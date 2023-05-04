#include <cstring>
#include <common_utils.h>
#include "common.h"
#include "callback.h"

#include "Preproc.h"

#include "system.h"

#define NEW_CHECK_RESULT(P, X, ERR)    {if ((X) > (P)) {MSDK_PRINT_RET_MSG(ERR); goto _exit;}}
#define DECODE_CHECK_RESULT(P, X, ERR)    {if ((X) > (P)) {MSDK_PRINT_RET_MSG(ERR); return -1;}}
#define DECODE_CHECK_ERROR(P, X, ERR)     {if ((X) == (P)) {MSDK_PRINT_RET_MSG(ERR); return -1;}}

class Converter {
public:
    Converter(MFXVideoSession *session): m_session(session)
    {
        m_mfxVPP = std::make_unique<MFXVideoVPP>(*m_session);
    }
    
    ~Converter()
    {
        if (m_mfxVPP) m_mfxVPP->Close();
    }

    mfxStatus Init(mfxFrameInfo &in, mfxFrameInfo &out)
    {
        mfxStatus sts = MFX_ERR_NONE;

        memset(&param, 0, sizeof(param));
        param.vpp.In = in;
        param.vpp.Out = out;

        param.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;

            // Query number of required surfaces for VPP
        mfxFrameAllocRequest VPPRequest; 
        memset(&VPPRequest, 0, sizeof(mfxFrameAllocRequest));
        sts = m_mfxVPP->QueryIOSurf(&param, &VPPRequest);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        VPPRequest.Type |= WILL_READ; // This line is only required for Windows DirectX11 to ensure that surfaces can be retrieved by the application

        // Allocate required surfaces
        mfxFrameAllocResponse mfxResponse;
        sts = mfxAllocator.Alloc(mfxAllocator.pthis, &VPPRequest, &mfxResponse);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

        mfxU16 nVPPSurfNum = mfxResponse.NumFrameActual;

        // Allocate surface headers (mfxFrameSurface1) for VPP
        pVPPSurfaces.resize(nVPPSurfNum);
        for (int i = 0; i < nVPPSurfNum; i++) {
            memset(&pVPPSurfaces[i], 0, sizeof(mfxFrameSurface1));
            pVPPSurfaces[i].Info = param.vpp.Out;
            pVPPSurfaces[i].Data.MemId = mfxResponse.mids[i];    // MID (memory id) represent one D3D NV12 surface
        }

         // Initialize Media SDK VPP
        sts = m_mfxVPP->Init(&param);
        MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    }

    mfxStatus Convert(mfxFrameSurface1 *in, mfxFrameSurface1 **out)
    {
        mfxStatus sts = MFX_ERR_NONE;
        mfxSyncPoint sync = NULL;
        int nSurfIdxIn = GetFreeSurfaceIndex(pVPPSurfaces);
        if (nSurfIdxIn == MFX_ERR_NOT_FOUND || nSurfIdxIn >= pVPPSurfaces.size()) {
            return MFX_ERR_NOT_FOUND;
        }
        mfxFrameSurface1 *free = &pVPPSurfaces[nSurfIdxIn];
        sts = m_mfxVPP->RunFrameVPPAsync(in, free, NULL, &sync);
        for (;;) {
            // Process a frame asychronously (returns immediately)
            sts = m_mfxVPP->RunFrameVPPAsync(in, free, NULL, &sync);
            if (MFX_WRN_DEVICE_BUSY == sts) {
                MSDK_SLEEP(1);  // Wait if device is busy, then repeat the same call
            } else
                break;
        }
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        sts = m_session->SyncOperation(sync, 1000);      // Synchronize. Wait until frame processing is ready
        MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        *out = free;
        return sts;
    }
private:
    std::unique_ptr<MFXVideoVPP> m_mfxVPP = NULL;
    MFXVideoSession *m_session;
    mfxVideoParam param;
    mfxFrameAllocator mfxAllocator;
    std::vector<mfxFrameSurface1> pVPPSurfaces;
};

class Decoder
{
public:
    mfxHDL m_hdl;
    MFXVideoSession session;
    MFXVideoDECODE *mfxDEC = NULL;
    std::unique_ptr<Converter> converter = NULL;
    std::vector<mfxU8> surfaceBuffersData;
    std::vector<mfxFrameSurface1> pmfxSurfaces;
    mfxVideoParam mfxVideoParams;
    bool initialized = false;
    mfxFrameAllocator mfxAllocator;
    std::unique_ptr<RGBToNV12> nv12torgb = NULL;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deviceCtx;
    ComPtr<ID3D11Texture2D> bgraTexture = NULL; 
    mfxFrameAllocResponse mfxResponse;

    Decoder(mfxHDL hdl): m_hdl(hdl) {
        device.Attach((ID3D11Device *)hdl);
        device->GetImmediateContext(deviceCtx.GetAddressOf());
        nv12torgb = std::make_unique<RGBToNV12>(device.Get(), deviceCtx.Get());
        nv12torgb->Init();
    }
};

extern "C" int intel_destroy_decoder(void* decoder)
{
    Decoder *p = (Decoder *)decoder;
    if (p)
    {
        if (p->mfxDEC)
        {
            p->mfxDEC->Close();
            delete p->mfxDEC;
        }
        mfx_common_Release();
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

#include "common_directx11.h"


extern "C" void* intel_new_decoder(void* hdl, API api, DataFormat codecID, SurfaceFormat outputSurfaceFormat)
{
    mfxStatus sts = MFX_ERR_NONE;
    mfxIMPL impl = MFX_IMPL_HARDWARE_ANY | MFX_IMPL_VIA_D3D11;
    mfxVersion ver = { {0, 1} };

    Decoder *p = new Decoder(hdl);
    mfx_common_SetHWDeviceContext(p->deviceCtx.Get());
    if (!p)
    {
        goto _exit;
    }

    sts = mfx_common_Initialize(hdl, impl, ver, &p->session, &p->mfxAllocator);
    NEW_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Create Media SDK decoder
    p->mfxDEC = new MFXVideoDECODE(p->session);
    p->converter = std::make_unique<Converter>(&p->session);


    memset(&p->mfxVideoParams, 0, sizeof(p->mfxVideoParams));
    if (!convert_codec(codecID, p->mfxVideoParams.mfx.CodecId))
    {
        goto _exit;
    }
    
    p->mfxVideoParams.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;

    // Validate video decode parameters (optional)
    sts = p->mfxDEC->Query(&p->mfxVideoParams, &p->mfxVideoParams);

    return p;

_exit:
    if (p)
    {
        intel_destroy_decoder(p);
        delete p;
    }
    return NULL;
}

static mfxStatus initialize(Decoder *p, mfxBitstream *mfxBS)
{
    mfxStatus sts = MFX_ERR_NONE;
    mfxFrameAllocRequest Request;
    memset(&Request, 0, sizeof(Request));
    mfxU16 numSurfaces;
    mfxU16 width, height;
    mfxU8 bitsPerPixel = 12; // NV12
    mfxU32 surfaceSize;
    mfxU8* surfaceBuffers;

    sts = p->mfxDEC->DecodeHeader(mfxBS, &p->mfxVideoParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    sts = p->mfxDEC->QueryIOSurf(&p->mfxVideoParams, &Request);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    numSurfaces = Request.NumFrameSuggested;

    Request.Type |= WILL_READ; // This line is only required for Windows DirectX11 to ensure that surfaces can be retrieved by the application

    // Allocate surfaces for decoder
    sts = p->mfxAllocator.Alloc(p->mfxAllocator.pthis, &Request, &p->mfxResponse);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Allocate surface headers (mfxFrameSurface1) for decoder
    p->pmfxSurfaces.resize(numSurfaces);
    for (int i = 0; i < numSurfaces; i++) {
        memset(&p->pmfxSurfaces[i], 0, sizeof(mfxFrameSurface1));
        p->pmfxSurfaces[i].Info = p->mfxVideoParams.mfx.FrameInfo;
        p->pmfxSurfaces[i].Data.MemId = p->mfxResponse.mids[i];      // MID (memory id) represents one video NV12 surface
    }

    // Initialize the Media SDK decoder
    sts = p->mfxDEC->Init(&p->mfxVideoParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    return MFX_ERR_NONE;
}

extern "C" int intel_decode(void* decoder, uint8_t *data, int len, DecodeCallback callback, void* obj)
{
    Decoder *p = (Decoder *)decoder;
    mfxStatus sts = MFX_ERR_NONE;
    mfxSyncPoint syncp;
    mfxFrameSurface1* pmfxOutSurface = NULL;
    mfxFrameSurface1* pmfxConvertSurface = NULL;
    int nIndex = 0;
    bool decoded = false;
    mfxBitstream mfxBS;
    mfxU32 decodedSize = 0;

    memset(&mfxBS, 0, sizeof(mfxBS));
    mfxBS.Data = data;
    mfxBS.DataLength = len;
    mfxBS.MaxLength = len;

    if (!p->initialized)
    {
        sts = initialize(p, &mfxBS);
        DECODE_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
        p->initialized = true;
    }

    decodedSize = mfxBS.DataLength;

    memset(&mfxBS, 0, sizeof(mfxBS));
    mfxBS.Data = data;
    mfxBS.DataLength = len;
    mfxBS.MaxLength = len;
    mfxBS.DataFlag = MFX_BITSTREAM_COMPLETE_FRAME;

    do
    {
        if (MFX_WRN_DEVICE_BUSY == sts)
            MSDK_SLEEP(1);  // Wait if device is busy, then repeat the same call to DecodeFrameAsync
        if (MFX_ERR_MORE_SURFACE == sts || MFX_ERR_NONE == sts) {
            nIndex = GetFreeSurfaceIndex(p->pmfxSurfaces);        // Find free frame surface
            MSDK_CHECK_ERROR(MFX_ERR_NOT_FOUND, nIndex, MFX_ERR_MEMORY_ALLOC);
        }
        // Decode a frame asychronously (returns immediately)
        //  - If input bitstream contains multiple frames DecodeFrameAsync will start decoding multiple frames, and remove them from bitstream
        sts = p->mfxDEC->DecodeFrameAsync(&mfxBS, &p->pmfxSurfaces[nIndex], &pmfxOutSurface, &syncp);

        // Ignore warnings if output is available,
        // if no output and no action required just repeat the DecodeFrameAsync call
        if (MFX_ERR_NONE < sts && syncp)
            sts = MFX_ERR_NONE;

        if (MFX_ERR_NONE == sts)
            sts = p->session.SyncOperation(syncp, 60000);      // Synchronize. Wait until decoded frame is ready

        if (MFX_ERR_NONE == sts)
        {
            mfxHDLPair pair = {NULL};
            sts = p->mfxAllocator.GetHDL(p->mfxAllocator.pthis, pmfxOutSurface->Data.MemId, (mfxHDL*)&pair);
            ID3D11Texture2D* texture = (ID3D11Texture2D*)pair.first;
            D3D11_TEXTURE2D_DESC desc2D;
            texture->GetDesc(&desc2D);
            if (!p->bgraTexture) {
                desc2D.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc2D.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                HRESULT hr = p->device->CreateTexture2D(&desc2D, NULL, p->bgraTexture.GetAddressOf());
                if (FAILED(hr)) return -1;
            }
            p->nv12torgb->Convert(texture, p->bgraTexture.Get());
            p->bgraTexture->GetDesc(&desc2D);
            if (MFX_ERR_NONE == sts) {
                callback(p->bgraTexture.Get(), SURFACE_FORMAT_BGRA, pmfxOutSurface->Info.CropW, pmfxOutSurface->Info.CropH, obj, 0);
                decoded = true;
            }
            break;
        }

    } 
    while(MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts || MFX_ERR_MORE_SURFACE == sts);

    return decoded ? 0 : -1;
}

