#include <cstring>
#include <common_utils.h>
#include "common.h"
#include "callback.h"

#define NEW_CHECK_RESULT(P, X, ERR)    {if ((X) > (P)) {MSDK_PRINT_RET_MSG(ERR); goto _exit;}}
#define DECODE_CHECK_RESULT(P, X, ERR)    {if ((X) > (P)) {MSDK_PRINT_RET_MSG(ERR); return -1;}}

struct Decoder
{
    MFXVideoSession session;
    MFXVideoDECODE *mfxDEC = NULL;
    std::vector<mfxU8> surfaceBuffersData;
    std::vector<mfxFrameSurface1> pmfxSurfaces;
    mfxVideoParam mfxVideoParams;
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
        Release();
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

extern "C" void* intel_new_decoder(HWDeviceType device, PixelFormat pixfmt, DataFormat codecID)
{
    mfxStatus sts = MFX_ERR_NONE;
    mfxIMPL impl = MFX_IMPL_HARDWARE_ANY;
    mfxVersion ver = { {0, 1} };

    Decoder *p = new Decoder();
    if (!p)
    {
        goto _exit;
    }

    sts = Initialize(impl, ver, &p->session, NULL);
    NEW_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Create Media SDK decoder
    p->mfxDEC = new MFXVideoDECODE(p->session);

    memset(&p->mfxVideoParams, 0, sizeof(p->mfxVideoParams));
    if (!convert_codec(codecID, p->mfxVideoParams.mfx.CodecId))
    {
        goto _exit;
    }
    
    p->mfxVideoParams.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;


    // Read a chunk of data from stream file into bit stream buffer
    // - Parse bit stream, searching for header and fill video parameters structure
    // - Abort if bit stream header is not found in the first bit stream buffer chunk
    // sts = ReadBitStreamData(&mfxBS, fSource.get());
    // MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Initialize the Media SDK decoder
    sts = p->mfxDEC->Init(&p->mfxVideoParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    NEW_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    return p;

_exit:
    if (p)
    {
        intel_destroy_decoder(p);
        delete p;
    }
    return NULL;
}

static void get_yuv_data(mfxFrameSurface1* pSurf, DecodeCallback callback, void* obj)
{
    mfxFrameInfo* pInfo = &pSurf->Info;
    mfxFrameData* pData = &pSurf->Data;
    // mfxU32 ChromaW = (pInfo->CropW % 2) ? (pInfo->CropW + 1) : pInfo->CropW;
    // mfxU32 ChromaH = (pInfo->CropH + 1) / 2;
    uint8_t* datas[MAX_DATA_NUM] = {0};
    int32_t linesizes[MAX_DATA_NUM] = {0};
    datas[0] = pData->Y + (pInfo->CropY * pData->Pitch + pInfo->CropX);
    linesizes[0] = pData->Pitch;
    datas[1] = pData->UV + (pInfo->CropY * pData->Pitch + pInfo->CropX);
    linesizes[1] = pData->Pitch;
}

extern "C" int intel_decode(void* decoder, uint8_t *data, int len, DecodeCallback callback, void* obj)
{
    Decoder *p = (Decoder *)decoder;
    mfxStatus sts = MFX_ERR_NONE;
    mfxSyncPoint syncp;
    mfxFrameSurface1* pmfxOutSurface = NULL;
    int nIndex = 0;
    bool decoded = false;
    mfxBitstream mfxBS;
    mfxFrameAllocRequest Request;
    memset(&Request, 0, sizeof(Request));
    mfxU16 numSurfaces;
    mfxU16 width, height;
    mfxU8 bitsPerPixel = 12; // NV12
    mfxU32 surfaceSize;
    mfxU8* surfaceBuffers;

    memset(&mfxBS, 0, sizeof(mfxBS));
    mfxBS.Data = data;
    mfxBS.DataLength = len;
    mfxBS.MaxLength = len;

    sts = p->mfxDEC->DecodeHeader(&mfxBS, &p->mfxVideoParams);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    sts = p->mfxDEC->QueryIOSurf(&p->mfxVideoParams, &Request);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    DECODE_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    numSurfaces = Request.NumFrameSuggested;

    // Allocate surfaces for decoder
    // - Width and height of buffer must be aligned, a multiple of 32
    // - Frame surface array keeps pointers all surface planes and general frame info
    width = (mfxU16) MSDK_ALIGN32(Request.Info.Width);
    height = (mfxU16) MSDK_ALIGN32(Request.Info.Height);
    surfaceSize = width * height * bitsPerPixel / 8;
    p->surfaceBuffersData.resize(surfaceSize * numSurfaces);
    surfaceBuffers = p->surfaceBuffersData.data();

    // Allocate surface headers (mfxFrameSurface1) for decoder
    p->pmfxSurfaces.resize(numSurfaces);
    for (int i = 0; i < numSurfaces; i++) {
        memset(&p->pmfxSurfaces[i], 0, sizeof(mfxFrameSurface1));
        p->pmfxSurfaces[i].Info = p->mfxVideoParams.mfx.FrameInfo;
        p->pmfxSurfaces[i].Data.Y = &surfaceBuffers[surfaceSize * i];
        p->pmfxSurfaces[i].Data.U = p->pmfxSurfaces[i].Data.Y + width * height;
        p->pmfxSurfaces[i].Data.V = p->pmfxSurfaces[i].Data.U + 1;
        p->pmfxSurfaces[i].Data.Pitch = width;
    }

    while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts || MFX_ERR_MORE_SURFACE == sts) {
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
            get_yuv_data(pmfxOutSurface, callback, obj);
            decoded = true;
        }
    }

    // MFX_ERR_MORE_DATA means that file has ended, need to go to buffering loop, exit in case of other errors
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
    DECODE_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    return decoded ? 0 : -1;
}

