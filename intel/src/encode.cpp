#include <cstring>
#include <common_utils.h>
#include "common.h"
#include "callback.h"

#include <dxgi.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_2.h>
#include <d3d11_3.h>
#include <d3d11_4.h>

#define NEW_CHECK_RESULT(P, X, ERR)    {if ((X) > (P)) {MSDK_PRINT_RET_MSG(ERR); return NULL;}}
#define ENCODE_CHECK_RESULT(P, X, ERR)    {if ((X) > (P)) {MSDK_PRINT_RET_MSG(ERR); return -1;}}
#define ENCODE_CHECK_ERROR(P, X, ERR)     {if ((X) == (P)) {MSDK_PRINT_RET_MSG(ERR); return -1;}}
struct Encoder
{
    MFXVideoSession session;
    MFXVideoENCODE *mfxENC = NULL;
    std::vector<mfxFrameSurface1> pEncSurfaces;
    std::vector<mfxU8> bstData;
    mfxBitstream mfxBS;
};

extern "C" int intel_driver_support()
{
    return 0;
}

extern "C" int intel_destroy_encoder(void *encoder)
{
    Encoder *p = (Encoder*)encoder;
    if (p)
    {
        if (p->mfxENC)
        {
            //  - It is recommended to close Media SDK components first, before releasing allocated surfaces, since
            //    some surfaces may still be locked by internal Media SDK resources.
            p->mfxENC->Close();
            delete p->mfxENC;
        }
        Release();
        // session closed automatically on destruction
    }
    return 0;
}

static bool convert_codec(DataFormat dataFormat, mfxU32 &CodecId)
{
    switch (dataFormat)
    {
    case H264: 
        CodecId = MFX_CODEC_AVC;
        return true;
    case H265:
        CodecId = MFX_CODEC_HEVC;
        return true;
    }
    return false;
}

static bool convert_pixfmt(PixelFormat pixfmt, mfxU32 &fourCC)
{
    switch (pixfmt)
    {
    case NV12:
        fourCC = MFX_FOURCC_NV12;
        return true;
    }
    return false;
}

static mfxStatus MFX_CDECL simple_getHDL(mfxHDL pthis, mfxMemId mid, mfxHDL* handle)
{
    mfxHDLPair* pair = (mfxHDLPair*)handle;
    pair->first = mid;
    pair->second = (mfxHDL)(UINT)0;
//	((ID3D11Texture2D*)mid)->AddRef();
    return MFX_ERR_NONE;
}

static mfxFrameAllocator alloc{
    {}, NULL,
    NULL,
    NULL,
    NULL,
    simple_getHDL,
    NULL
};

extern "C" void* intel_new_encoder(ID3D11Device *pD3dDevice, HWDeviceType deviceType,
                        DataFormat dataFormat, int32_t w, int32_t h, 
                        int32_t kbs, int32_t framerate, int32_t gop)
{
    mfxStatus sts = MFX_ERR_NONE;
    mfxInitParam mfxparams{};
    mfxIMPL impl = MFX_IMPL_HARDWARE_ANY | MFX_IMPL_VIA_D3D11;
    mfxparams.Implementation = impl;
    mfxparams.Version.Major = 1;
    mfxparams.Version.Minor = 0;
    mfxparams.GPUCopy = MFX_GPUCOPY_OFF;
    mfxVersion ver = { { 0, 1 } };
    mfxVideoParam mfxEncParams;
    memset(&mfxEncParams, 0, sizeof(mfxEncParams));
    mfxFrameAllocRequest EncRequest;
    memset(&EncRequest, 0, sizeof(EncRequest));
    mfxU16 nEncSurfNum;
    mfxU16 width, height;
    mfxU8 bitsPerPixel = 12; // NV12 
    mfxU32 surfaceSize;
    mfxU8* surfaceBuffers;
    mfxVideoParam par;
    memset(&par, 0, sizeof(par));

    if (!convert_codec(dataFormat, mfxEncParams.mfx.CodecId))
    {
        return NULL;
    }
    mfxEncParams.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
    mfxEncParams.mfx.TargetKbps = kbs;
    mfxEncParams.mfx.RateControlMethod = MFX_RATECONTROL_CBR;
    mfxEncParams.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    mfxEncParams.mfx.FrameInfo.FrameRateExtN = framerate;
    mfxEncParams.mfx.FrameInfo.FrameRateExtD = 1;
    mfxEncParams.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    mfxEncParams.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    mfxEncParams.mfx.FrameInfo.CropX = 0;
    mfxEncParams.mfx.FrameInfo.CropY = 0;
    mfxEncParams.mfx.FrameInfo.CropW = w;
    mfxEncParams.mfx.FrameInfo.CropH = h;
    // Width must be a multiple of 16
    // Height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture
    mfxEncParams.mfx.FrameInfo.Width = MSDK_ALIGN16(w); // todo
    mfxEncParams.mfx.FrameInfo.Height =
        (MFX_PICSTRUCT_PROGRESSIVE == mfxEncParams.mfx.FrameInfo.PicStruct) ?
        MSDK_ALIGN16(h) :
        MSDK_ALIGN32(h);
    mfxEncParams.mfx.EncodedOrder = 0;

    mfxEncParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;

    // Configuration for low latency
    mfxEncParams.AsyncDepth = 1;        //1 is best for low latency
    mfxEncParams.mfx.GopRefDist = 1;    //1 is best for low latency, I and P frames only

    Encoder *p = new Encoder();
    if (!p)
    {
        goto _exit;
    }

    // sts = Initialize(impl, ver, &p->session, NULL);
    sts = p->session.InitEx(mfxparams);
    NEW_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    sts = p->session.SetHandle(MFX_HANDLE_D3D11_DEVICE, pD3dDevice);
    NEW_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    sts = p->session.SetFrameAllocator(&alloc);
    NEW_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Create Media SDK encoder
    p->mfxENC = new MFXVideoENCODE(p->session);
    if (!p->mfxENC)
    {
        goto _exit;
    }

    // Validate video encode parameters (optional)
    // - In this example the validation result is written to same structure
    // - MFX_WRN_INCOMPATIBLE_VIDEO_PARAM is returned if some of the video parameters are not supported,
    //   instead the encoder will select suitable parameters closest matching the requested configuration
    sts = p->mfxENC->Query(&mfxEncParams, &mfxEncParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_INCOMPATIBLE_VIDEO_PARAM);
    NEW_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    sts = p->mfxENC->QueryIOSurf(&mfxEncParams, &EncRequest);
    NEW_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    nEncSurfNum = EncRequest.NumFrameSuggested;

    // Allocate surface headers (mfxFrameSurface1) for encoder
    p->pEncSurfaces.resize(nEncSurfNum);
    for (int i = 0; i < nEncSurfNum; i++) {
        memset(&p->pEncSurfaces[i], 0, sizeof(mfxFrameSurface1));
        p->pEncSurfaces[i].Info = mfxEncParams.mfx.FrameInfo;
    }

    // Initialize the Media SDK encoder
    sts = p->mfxENC->Init(&mfxEncParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    NEW_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Retrieve video parameters selected by encoder.
    // - BufferSizeInKB parameter is required to set bit stream buffer size
    sts = p->mfxENC->GetVideoParam(&par);
    NEW_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Prepare Media SDK bit stream buffer
    memset(&p->mfxBS, 0, sizeof(p->mfxBS));
    p->mfxBS.MaxLength = par.mfx.BufferSizeInKB * 1024;
    p->bstData.resize(p->mfxBS.MaxLength);
    p->mfxBS.Data = p->bstData.data();

    return p;
_exit:
    if (p)
    {
        intel_destroy_encoder(p);
        delete p;
    }
    return NULL;
}

extern "C" int intel_encode(void *encoder,  ID3D11Texture2D* tex,
                            EncodeCallback callback, void* obj)
{
    mfxStatus sts = MFX_ERR_NONE;
    bool encoded = false;
    Encoder *p = (Encoder*)encoder;
    int nEncSurfIdx = 0;
    mfxSyncPoint syncp;

    p->mfxBS.DataLength = 0;
    p->mfxBS.DataOffset = 0;

    nEncSurfIdx = GetFreeSurfaceIndex(p->pEncSurfaces);   // Find free frame surface
    ENCODE_CHECK_ERROR(MFX_ERR_NOT_FOUND, nEncSurfIdx, MFX_ERR_MEMORY_ALLOC);

    p->pEncSurfaces[nEncSurfIdx].Data.MemId = tex;

    for (;;) {
        // Encode a frame asychronously (returns immediately)
        sts = p->mfxENC->EncodeFrameAsync(NULL, &p->pEncSurfaces[nEncSurfIdx], &p->mfxBS, &syncp);

        if (MFX_ERR_NONE < sts && !syncp) {     // Repeat the call if warning and no output
            if (MFX_WRN_DEVICE_BUSY == sts)
                MSDK_SLEEP(1);  // Wait if device is busy, then repeat the same call
        } else if (MFX_ERR_NONE < sts && syncp) {
            sts = MFX_ERR_NONE;     // Ignore warnings if output is available
            break;
        } else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts) {
            // Allocate more bitstream buffer memory here if needed...
            break;
        } else
            break;
    }

    if (MFX_ERR_NONE == sts) {
        sts = p->session.SyncOperation(syncp, 60000);      // Synchronize. Wait until encoded frame is ready
        ENCODE_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        if (p->mfxBS.DataLength > 0)
        {
            callback(p->mfxBS.Data + p->mfxBS.DataOffset, p->mfxBS.DataLength, 0, 0, obj);
            encoded = true;
        }
    }

    // MFX_ERR_MORE_DATA means that the input file has ended, need to go to buffering loop, exit in case of other errors
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
    ENCODE_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    return encoded ? 0 : -1;

}


extern "C" int intel_set_bitrate(void *e, int32_t kbs)
{
    return -1;
}

extern "C" int intel_set_framerate(void *e, int32_t framerate)
{
    return -1;
}