#include <cstring>

#include <Preproc.h>
#include <d3d11_allocator.h>
#include <sample_defs.h>
#include <sample_utils.h>

#include "callback.h"
#include "common.h"
#include "system.h"

#define LOG_MODULE "MFX DECODE"
#include "log.h"

#define CHECK_STATUS_GOTO(X, MSG)                                              \
  {                                                                            \
    if ((X) < MFX_ERR_NONE) {                                                  \
      MSDK_PRINT_RET_MSG(X, MSG);                                              \
      LOG_ERROR(MSG + "failed, sts=" + std::to_string((int)X));                \
      goto _exit;                                                              \
    }                                                                          \
  }
#define CHECK_STATUS_RETURN(X, MSG)                                            \
  {                                                                            \
    if ((X) < MFX_ERR_NONE) {                                                  \
      MSDK_PRINT_RET_MSG(X, MSG);                                              \
      LOG_ERROR(MSG + "failed, sts=" + std::to_string((int)X));                \
      return X;                                                                \
    }                                                                          \
  }

#define LOG_MSDK_CHECK_STATUS(X, MSG)                                          \
  {                                                                            \
    if ((X) < MFX_ERR_NONE) {                                                  \
      MSDK_PRINT_RET_MSG(X, MSG);                                              \
      LOG_ERROR(MSG + "failed, sts=" + std::to_string((int)X));                \
      return X;                                                                \
    }                                                                          \
  }

namespace {

class MFXDecoder {
public:
  std::unique_ptr<NativeDevice> native_ = nullptr;
  MFXVideoSession session_;
  MFXVideoDECODE *mfxDEC_ = NULL;
  std::vector<mfxFrameSurface1> pmfxSurfaces_;
  mfxVideoParam mfxVideoParams_;
  bool initialized_ = false;
  D3D11FrameAllocator d3d11FrameAllocator_;
  std::unique_ptr<RGBToNV12> nv12torgb_ = NULL;
  mfxFrameAllocResponse mfxResponse_;
  bool outputSharedHandle_;

  mfxStatus InitializeMFX() {
    mfxStatus sts = MFX_ERR_NONE;
    mfxIMPL impl = MFX_IMPL_HARDWARE_ANY | MFX_IMPL_VIA_D3D11;
    mfxVersion ver = {{0, 1}};
    D3D11AllocatorParams allocParams;

    sts = session_.Init(impl, &ver);
    LOG_MSDK_CHECK_STATUS(sts, "session Init");

    sts = session_.SetHandle(MFX_HANDLE_D3D11_DEVICE, native_->device_.Get());
    LOG_MSDK_CHECK_STATUS(sts, "SetHandle");

    allocParams.bUseSingleTexture = false; // important
    allocParams.pDevice = native_->device_.Get();
    allocParams.uncompressedResourceMiscFlags = 0;
    sts = d3d11FrameAllocator_.Init(&allocParams);
    LOG_MSDK_CHECK_STATUS(sts, "init D3D11FrameAllocator");

    sts = session_.SetFrameAllocator(&d3d11FrameAllocator_);
    LOG_MSDK_CHECK_STATUS(sts, "SetFrameAllocator");

    return MFX_ERR_NONE;
  }

  mfxStatus initialize(mfxBitstream *mfxBS) {
    mfxStatus sts = MFX_ERR_NONE;
    mfxFrameAllocRequest Request;
    memset(&Request, 0, sizeof(Request));
    mfxU16 numSurfaces;
    mfxU16 width, height;
    mfxU8 bitsPerPixel = 12; // NV12
    mfxU32 surfaceSize;
    mfxU8 *surfaceBuffers;

    sts = mfxDEC_->DecodeHeader(mfxBS, &mfxVideoParams_);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    sts = mfxDEC_->QueryIOSurf(&mfxVideoParams_, &Request);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    numSurfaces = Request.NumFrameSuggested;

    // Request.Type |= WILL_READ; // This line is only required for Windows
    // DirectX11 to ensure that surfaces can be retrieved by the application

    // Allocate surfaces for decoder
    sts = d3d11FrameAllocator_.AllocFrames(&Request, &mfxResponse_);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

    // Allocate surface headers (mfxFrameSurface1) for decoder
    pmfxSurfaces_.resize(numSurfaces);
    for (int i = 0; i < numSurfaces; i++) {
      memset(&pmfxSurfaces_[i], 0, sizeof(mfxFrameSurface1));
      pmfxSurfaces_[i].Info = mfxVideoParams_.mfx.FrameInfo;
      pmfxSurfaces_[i].Data.MemId =
          mfxResponse_
              .mids[i]; // MID (memory id) represents one video NV12 surface
    }

    // Initialize the Media SDK decoder
    sts = mfxDEC_->Init(&mfxVideoParams_);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
    return MFX_ERR_NONE;
  }
};

bool convert_codec(DataFormat dataFormat, mfxU32 &CodecId) {
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

} // namespace

extern "C" {

int mfx_destroy_decoder(void *decoder) {
  MFXDecoder *p = (MFXDecoder *)decoder;
  if (p) {
    if (p->mfxDEC_) {
      p->mfxDEC_->Close();
      delete p->mfxDEC_;
    }
  }
  return 0;
}

void *mfx_new_decoder(void *device, int64_t luid, API api, DataFormat codecID,
                      bool outputSharedHandle) {
  mfxStatus sts = MFX_ERR_NONE;

  MFXDecoder *p = new MFXDecoder();
  p->native_ = std::make_unique<NativeDevice>();
  if (!p->native_->Init(luid, (ID3D11Device *)device, 4))
    goto _exit;
  p->nv12torgb_ = std::make_unique<RGBToNV12>(p->native_->device_.Get(),
                                              p->native_->context_.Get());
  if (FAILED(p->nv12torgb_->Init())) {
    goto _exit;
  }
  if (!p) {
    goto _exit;
  }
  sts = p->InitializeMFX();
  CHECK_STATUS_GOTO(sts, "InitializeMFX");

  // Create Media SDK decoder
  p->mfxDEC_ = new MFXVideoDECODE(p->session_);

  memset(&p->mfxVideoParams_, 0, sizeof(p->mfxVideoParams_));
  if (!convert_codec(codecID, p->mfxVideoParams_.mfx.CodecId)) {
    goto _exit;
  }

  p->mfxVideoParams_.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;
  // AsyncDepth: sSpecifies how many asynchronous operations an
  // application performs before the application explicitly synchronizes the
  // result. If zero, the value is not specified
  p->mfxVideoParams_.AsyncDepth = 1; // Not important.
  // DecodedOrder: For AVC and HEVC, used to instruct the decoder
  // to return output frames in the decoded order. Must be zero for all other
  // decoders.
  p->mfxVideoParams_.mfx.DecodedOrder = true; // Not important.

  // Validate video decode parameters (optional)
  sts = p->mfxDEC_->Query(&p->mfxVideoParams_, &p->mfxVideoParams_);
  p->outputSharedHandle_ = outputSharedHandle;

  return p;

_exit:
  if (p) {
    mfx_destroy_decoder(p);
    delete p;
  }
  return NULL;
}

int mfx_decode(void *decoder, uint8_t *data, int len, DecodeCallback callback,
               void *obj) {
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

  if (!p->initialized_) {
    sts = p->initialize(&mfxBS);
    CHECK_STATUS_RETURN(sts, "initialize");
    p->initialized_ = true;
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
      std::cerr << "mfx decode loop two many times" << std::endl;
      break;
    }

    if (MFX_WRN_DEVICE_BUSY == sts)
      MSDK_SLEEP(1); // Wait if device is busy, then repeat the same call to
                     // DecodeFrameAsync
    if (MFX_ERR_MORE_SURFACE == sts || MFX_ERR_NONE == sts) {
      nIndex = GetFreeSurfaceIndex(
          p->pmfxSurfaces_.data(),
          p->pmfxSurfaces_.size()); // Find free frame surface
      if (nIndex >= p->pmfxSurfaces_.size()) {
        std::cerr << "GetFreeSurfaceIndex failed, nIndex=" << nIndex
                  << std::endl;
        return -1;
      }
    }
    // Decode a frame asychronously (returns immediately)
    //  - If input bitstream contains multiple frames DecodeFrameAsync will
    //  start decoding multiple frames, and remove them from bitstream
    sts = p->mfxDEC_->DecodeFrameAsync(&mfxBS, &p->pmfxSurfaces_[nIndex],
                                       &pmfxOutSurface, &syncp);

    // Ignore warnings if output is available,
    // if no output and no action required just repeat the DecodeFrameAsync call
    if (MFX_ERR_NONE < sts && syncp)
      sts = MFX_ERR_NONE;

    if (MFX_ERR_NONE == sts)
      sts = p->session_.SyncOperation(
          syncp, 1000); // Synchronize. Wait until decoded frame is ready

    if (MFX_ERR_NONE == sts) {
      mfxHDLPair pair = {NULL};
      sts = p->d3d11FrameAllocator_.GetFrameHDL(pmfxOutSurface->Data.MemId,
                                                (mfxHDL *)&pair);
      ID3D11Texture2D *texture = (ID3D11Texture2D *)pair.first;
      D3D11_TEXTURE2D_DESC desc2D;
      texture->GetDesc(&desc2D);
      if (!p->native_->EnsureTexture(desc2D.Width, desc2D.Height)) {
        std::cerr << "Failed to EnsureTexture" << std::endl;
        return -1;
      }
      p->native_->next(); // comment out to remove picture shaking
      p->native_->BeginQuery();
      if (FAILED(p->nv12torgb_->Convert(texture,
                                        p->native_->GetCurrentTexture()))) {
        std::cerr << "Failed to convert to bgra" << std::endl;
        p->native_->EndQuery();
        return -1;
      }
      p->native_->context_->Flush();
      p->native_->EndQuery();
      if (!p->native_->Query()) {
        std::cerr << "Failed to query" << std::endl;
      }
      void *output = nullptr;
      if (p->outputSharedHandle_) {
        HANDLE sharedHandle = p->native_->GetSharedHandle();
        if (!sharedHandle) {
          std::cerr << "Failed to GetSharedHandle" << std::endl;
          return -1;
        }
        output = sharedHandle;
      } else {
        output = p->native_->GetCurrentTexture();
      }

      if (MFX_ERR_NONE == sts) {
        if (callback)
          callback(output, obj);
        decoded = true;
      }
      break;
    }

  } while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_SURFACE == sts);

  if (!decoded) {
    std::cerr << "decoded failed, sts=" << sts << std::endl;
  }

  return decoded ? 0 : -1;
}

int mfx_test_decode(AdapterDesc *outDescs, int32_t maxDescNum,
                    int32_t *outDescNum, API api, DataFormat dataFormat,
                    bool outputSharedHandle, uint8_t *data, int32_t length) {
  try {
    AdapterDesc *descs = (AdapterDesc *)outDescs;
    Adapters adapters;
    if (!adapters.Init(ADAPTER_VENDOR_INTEL))
      return -1;
    int count = 0;
    for (auto &adapter : adapters.adapters_) {
      MFXDecoder *p =
          (MFXDecoder *)mfx_new_decoder(nullptr, LUID(adapter.get()->desc1_),
                                        api, dataFormat, outputSharedHandle);
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
} // extern "C"
