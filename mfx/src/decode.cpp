#include <cstring>

#include <Preproc.h>
#include <d3d11_allocator.h>
#include <sample_defs.h>
#include <sample_utils.h>

#include "callback.h"
#include "common.h"
#include "system.h"

#define CHECK_STATUS_GOTO(X, MSG)                                              \
  {                                                                            \
    if ((X) < MFX_ERR_NONE) {                                                  \
      MSDK_PRINT_RET_MSG(X, MSG);                                              \
      goto _exit;                                                              \
    }                                                                          \
  }
#define CHECK_STATUS_RETURN(X, MSG)                                            \
  {                                                                            \
    if ((X) < MFX_ERR_NONE) {                                                  \
      MSDK_PRINT_RET_MSG(X, MSG);                                              \
      return X;                                                                \
    }                                                                          \
  }

struct MFXDecoder {
public:
  std::unique_ptr<NativeDevice> nativeDevice = nullptr;
  MFXVideoSession session;
  MFXVideoDECODE *mfxDEC = NULL;
  std::vector<mfxU8> surfaceBuffersData;
  std::vector<mfxFrameSurface1> pmfxSurfaces;
  mfxVideoParam mfxVideoParams;
  bool initialized = false;
  D3D11FrameAllocator d3d11FrameAllocator;
  std::unique_ptr<RGBToNV12> nv12torgb = NULL;
  mfxFrameAllocResponse mfxResponse;
};

extern "C" int mfx_destroy_decoder(void *decoder) {
  MFXDecoder *p = (MFXDecoder *)decoder;
  if (p) {
    if (p->mfxDEC) {
      p->mfxDEC->Close();
      delete p->mfxDEC;
    }
  }
  return 0;
}

static bool convert_codec(DataFormat dataFormat, mfxU32 &CodecId) {
  switch (dataFormat) {
  case H264:
    CodecId = MFX_CODEC_AVC;
    return true;
  case H265:
    CodecId = MFX_CODEC_HEVC;
    return true;
  }
  return false;
}

static mfxStatus InitializeMFX(MFXDecoder *p) {
  mfxStatus sts = MFX_ERR_NONE;
  mfxIMPL impl = MFX_IMPL_HARDWARE_ANY | MFX_IMPL_VIA_D3D11;
  mfxVersion ver = {{0, 1}};
  D3D11AllocatorParams allocParams;

  sts = p->session.Init(impl, &ver);
  MSDK_CHECK_STATUS(sts, "session Init");

  sts = p->session.SetHandle(MFX_HANDLE_D3D11_DEVICE,
                             p->nativeDevice->device_.Get());
  MSDK_CHECK_STATUS(sts, "SetHandle");

  allocParams.bUseSingleTexture = true;
  allocParams.pDevice = p->nativeDevice->device_.Get();
  allocParams.uncompressedResourceMiscFlags = 0;
  sts = p->d3d11FrameAllocator.Init(&allocParams);
  MSDK_CHECK_STATUS(sts, "init D3D11FrameAllocator");

  sts = p->session.SetFrameAllocator(&p->d3d11FrameAllocator);
  MSDK_CHECK_STATUS(sts, "SetFrameAllocator");

  return MFX_ERR_NONE;
}

extern "C" void *mfx_new_decoder(void *device, int64_t luid, API api,
                                 DataFormat codecID,
                                 SurfaceFormat outputSurfaceFormat) {
  mfxStatus sts = MFX_ERR_NONE;

  MFXDecoder *p = new MFXDecoder();
  p->nativeDevice = std::make_unique<NativeDevice>();
  if (!p->nativeDevice->Init(luid, (ID3D11Device *)device))
    goto _exit;
  p->nv12torgb = std::make_unique<RGBToNV12>(p->nativeDevice->device_.Get(),
                                             p->nativeDevice->context_.Get());
  if (FAILED(p->nv12torgb->Init())) {
    goto _exit;
  }
  if (!p) {
    goto _exit;
  }
  sts = InitializeMFX(p);
  CHECK_STATUS_GOTO(sts, "InitializeMFX");

  // Create Media SDK decoder
  p->mfxDEC = new MFXVideoDECODE(p->session);

  memset(&p->mfxVideoParams, 0, sizeof(p->mfxVideoParams));
  if (!convert_codec(codecID, p->mfxVideoParams.mfx.CodecId)) {
    goto _exit;
  }

  p->mfxVideoParams.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;

  // Validate video decode parameters (optional)
  sts = p->mfxDEC->Query(&p->mfxVideoParams, &p->mfxVideoParams);

  return p;

_exit:
  if (p) {
    mfx_destroy_decoder(p);
    delete p;
  }
  return NULL;
}

static mfxStatus initialize(MFXDecoder *p, mfxBitstream *mfxBS) {
  mfxStatus sts = MFX_ERR_NONE;
  mfxFrameAllocRequest Request;
  memset(&Request, 0, sizeof(Request));
  mfxU16 numSurfaces;
  mfxU16 width, height;
  mfxU8 bitsPerPixel = 12; // NV12
  mfxU32 surfaceSize;
  mfxU8 *surfaceBuffers;

  sts = p->mfxDEC->DecodeHeader(mfxBS, &p->mfxVideoParams);
  MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
  MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

  sts = p->mfxDEC->QueryIOSurf(&p->mfxVideoParams, &Request);
  MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
  MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

  numSurfaces = Request.NumFrameSuggested;

  // Request.Type |= WILL_READ; // This line is only required for Windows
  // DirectX11 to ensure that surfaces can be retrieved by the application

  // Allocate surfaces for decoder
  sts = p->d3d11FrameAllocator.AllocFrames(&Request, &p->mfxResponse);
  MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

  // Allocate surface headers (mfxFrameSurface1) for decoder
  p->pmfxSurfaces.resize(numSurfaces);
  for (int i = 0; i < numSurfaces; i++) {
    memset(&p->pmfxSurfaces[i], 0, sizeof(mfxFrameSurface1));
    p->pmfxSurfaces[i].Info = p->mfxVideoParams.mfx.FrameInfo;
    p->pmfxSurfaces[i].Data.MemId =
        p->mfxResponse
            .mids[i]; // MID (memory id) represents one video NV12 surface
  }

  // Initialize the Media SDK decoder
  sts = p->mfxDEC->Init(&p->mfxVideoParams);
  MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
  MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
  return MFX_ERR_NONE;
}

extern "C" int mfx_decode(void *decoder, uint8_t *data, int len,
                          DecodeCallback callback, void *obj) {
  MFXDecoder *p = (MFXDecoder *)decoder;
  mfxStatus sts = MFX_ERR_NONE;
  mfxSyncPoint syncp;
  mfxFrameSurface1 *pmfxOutSurface = NULL;
  mfxFrameSurface1 *pmfxConvertSurface = NULL;
  int nIndex = 0;
  bool decoded = false;
  mfxBitstream mfxBS;
  mfxU32 decodedSize = 0;

  memset(&mfxBS, 0, sizeof(mfxBS));
  mfxBS.Data = data;
  mfxBS.DataLength = len;
  mfxBS.MaxLength = len;

  if (!p->initialized) {
    sts = initialize(p, &mfxBS);
    CHECK_STATUS_RETURN(sts, "initialize");
    p->initialized = true;
  }

  decodedSize = mfxBS.DataLength;

  memset(&mfxBS, 0, sizeof(mfxBS));
  mfxBS.Data = data;
  mfxBS.DataLength = len;
  mfxBS.MaxLength = len;
  mfxBS.DataFlag = MFX_BITSTREAM_COMPLETE_FRAME;
  int loop_counter = 0;

  do {
    if (loop_counter++ > 100) {
      printf("mfx decode loop two many times\n");
      break;
    }

    if (MFX_WRN_DEVICE_BUSY == sts)
      MSDK_SLEEP(1); // Wait if device is busy, then repeat the same call to
                     // DecodeFrameAsync
    if (MFX_ERR_MORE_SURFACE == sts || MFX_ERR_NONE == sts) {
      nIndex = GetFreeSurfaceIndex(
          p->pmfxSurfaces.data(),
          p->pmfxSurfaces.size()); // Find free frame surface
      if (nIndex >= p->pmfxSurfaces.size())
        return -1;
    }
    // Decode a frame asychronously (returns immediately)
    //  - If input bitstream contains multiple frames DecodeFrameAsync will
    //  start decoding multiple frames, and remove them from bitstream
    sts = p->mfxDEC->DecodeFrameAsync(&mfxBS, &p->pmfxSurfaces[nIndex],
                                      &pmfxOutSurface, &syncp);

    // Ignore warnings if output is available,
    // if no output and no action required just repeat the DecodeFrameAsync call
    if (MFX_ERR_NONE < sts && syncp)
      sts = MFX_ERR_NONE;

    if (MFX_ERR_NONE == sts)
      sts = p->session.SyncOperation(
          syncp, 1000); // Synchronize. Wait until decoded frame is ready

    if (MFX_ERR_NONE == sts) {
      mfxHDLPair pair = {NULL};
      sts = p->d3d11FrameAllocator.GetFrameHDL(pmfxOutSurface->Data.MemId,
                                               (mfxHDL *)&pair);
      ID3D11Texture2D *texture = (ID3D11Texture2D *)pair.first;
      D3D11_TEXTURE2D_DESC desc2D;
      texture->GetDesc(&desc2D);
      if (!p->nativeDevice->EnsureTexture(desc2D.Width, desc2D.Height)) {
        std::cerr << "Failed to EnsureTexture" << std::endl;
        return -1;
      }
      p->nativeDevice->next(); // comment out to remove picture shaking
      HRI(p->nv12torgb->Convert(texture, p->nativeDevice->GetCurrentTexture()));
      HANDLE sharedHandle = p->nativeDevice->GetSharedHandle();
      if (!sharedHandle) {
        std::cerr << "Failed to GetSharedHandle" << std::endl;
        return -1;
      }
      if (MFX_ERR_NONE == sts) {
        if (callback)
          callback(sharedHandle, obj);
        decoded = true;
      }
      break;
    }

  } while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_SURFACE == sts);

  return decoded ? 0 : -1;
}

extern "C" int mfx_test_decode(AdapterDesc *outDescs, int32_t maxDescNum,
                               int32_t *outDescNum, API api,
                               DataFormat dataFormat,
                               SurfaceFormat outputSurfaceFormat, uint8_t *data,
                               int32_t length) {
  try {
    AdapterDesc *descs = (AdapterDesc *)outDescs;
    Adapters adapters;
    if (!adapters.Init(ADAPTER_VENDOR_INTEL))
      return -1;
    int count = 0;
    for (auto &adapter : adapters.adapters_) {
      MFXDecoder *p =
          (MFXDecoder *)mfx_new_decoder(nullptr, LUID(adapter.get()->desc1_),
                                        api, dataFormat, outputSurfaceFormat);
      if (!p)
        continue;
      if (mfx_decode(p, data, length, nullptr, nullptr) == 0) {
        AdapterDesc *desc = descs + count;
        desc->luid = LUID(adapter.get()->desc1_);
        count += 1;
        if (count >= maxDescNum)
          break;
      }
    }
    *outDescNum = count;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  }
  return -1;
}