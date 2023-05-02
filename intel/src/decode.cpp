#include <cstring>
#include <common_utils.h>
#include "common.h"
#include "callback.h"

#define NEW_CHECK_RESULT(P, X, ERR)    {if ((X) > (P)) {MSDK_PRINT_RET_MSG(ERR); goto _exit;}}
#define DECODE_CHECK_RESULT(P, X, ERR)    {if ((X) > (P)) {MSDK_PRINT_RET_MSG(ERR); return -1;}}
#define DECODE_CHECK_ERROR(P, X, ERR)     {if ((X) == (P)) {MSDK_PRINT_RET_MSG(ERR); return -1;}}

struct Decoder
{
    MFXVideoSession session;
    MFXVideoDECODE *mfxDEC = NULL;
    std::vector<mfxU8> surfaceBuffersData;
    std::vector<mfxFrameSurface1> pmfxSurfaces;
    mfxVideoParam mfxVideoParams;
    bool initialized = false;
    mfxFrameAllocator mfxAllocator;
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

extern "C" void* intel_new_decoder(void* hdl, API api, DataFormat codecID, SurfaceFormat outputSurfaceFormat)
{
    mfxStatus sts = MFX_ERR_NONE;
    mfxIMPL impl = MFX_IMPL_HARDWARE_ANY | MFX_IMPL_VIA_D3D11;
    mfxVersion ver = { {0, 1} };

    Decoder *p = new Decoder();
    if (!p)
    {
        goto _exit;
    }

    sts = Initialize(impl, ver, &p->session, &p->mfxAllocator);
    NEW_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Create Media SDK decoder
    p->mfxDEC = new MFXVideoDECODE(p->session);

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
    mfxFrameAllocResponse mfxResponse;
    sts = p->mfxAllocator.Alloc(p->mfxAllocator.pthis, &Request, &mfxResponse);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Allocate surface headers (mfxFrameSurface1) for decoder
    p->pmfxSurfaces.resize(numSurfaces);
    for (int i = 0; i < numSurfaces; i++) {
        memset(&p->pmfxSurfaces[i], 0, sizeof(mfxFrameSurface1));
        p->pmfxSurfaces[i].Info = p->mfxVideoParams.mfx.FrameInfo;
        p->pmfxSurfaces[i].Data.MemId = mfxResponse.mids[i];      // MID (memory id) represents one video NV12 surface
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
            decoded = true;
            break;
        }

    } 
    while(MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts || MFX_ERR_MORE_SURFACE == sts);

    return decoded ? 0 : -1;
}

