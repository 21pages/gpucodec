#include <cstring>
#include <iostream>
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

namespace {

mfxStatus MFX_CDECL simple_getHDL(mfxHDL pthis, mfxMemId mid, mfxHDL *handle) {
  mfxHDLPair *pair = (mfxHDLPair *)handle;
  pair->first = mid;
  pair->second = (mfxHDL)(UINT)0;
  return MFX_ERR_NONE;
}

mfxFrameAllocator alloc{{}, NULL, NULL, NULL, NULL, simple_getHDL, NULL};

mfxStatus InitSession(MFXVideoSession &session) {
  mfxInitParam mfxparams{};
  mfxIMPL impl = MFX_IMPL_HARDWARE_ANY | MFX_IMPL_VIA_D3D11;
  mfxparams.Implementation = impl;
  mfxparams.Version.Major = 1;
  mfxparams.Version.Minor = 0;
  mfxparams.GPUCopy = MFX_GPUCOPY_OFF;

  return session.InitEx(mfxparams);
}
class MFXEncoder {
public:
  std::unique_ptr<NativeDevice> native_ = nullptr;
  MFXVideoSession session_;
  MFXVideoENCODE *mfxENC_ = nullptr;
  std::vector<mfxFrameSurface1> pEncSurfaces_;
  std::vector<mfxU8> bstData_;
  mfxBitstream mfxBS_;
  int width_ = 0;
  int height_ = 0;

  mfxStatus InitMFX() {
    mfxStatus sts = MFX_ERR_NONE;

    sts = InitSession(session_);
    MSDK_CHECK_STATUS(sts, "InitSession");
    sts = session_.SetHandle(MFX_HANDLE_D3D11_DEVICE, native_->device_.Get());
    MSDK_CHECK_STATUS(sts, "SetHandle");
    sts = session_.SetFrameAllocator(&alloc);
    MSDK_CHECK_STATUS(sts, "SetFrameAllocator");

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

int mfx_driver_support() {
  MFXVideoSession session;
  return InitSession(session) == MFX_ERR_NONE ? 0 : -1;
}

int mfx_destroy_encoder(void *encoder) {
  MFXEncoder *p = (MFXEncoder *)encoder;
  if (p) {
    if (p->mfxENC_) {
      //  - It is recommended to close Media SDK components first, before
      //  releasing allocated surfaces, since
      //    some surfaces may still be locked by internal Media SDK resources.
      p->mfxENC_->Close();
      delete p->mfxENC_;
    }
    // session closed automatically on destruction
  }
  return 0;
}

void *mfx_new_encoder(void *handle, int64_t luid, API api,
                      DataFormat dataFormat, int32_t w, int32_t h, int32_t kbs,
                      int32_t framerate, int32_t gop) {
  mfxStatus sts = MFX_ERR_NONE;
  mfxVersion ver = {{0, 1}};
  mfxVideoParam mfxEncParams;
  memset(&mfxEncParams, 0, sizeof(mfxEncParams));
  mfxFrameAllocRequest EncRequest;
  memset(&EncRequest, 0, sizeof(EncRequest));
  mfxU16 nEncSurfNum;
  mfxU16 width, height;
  mfxU32 surfaceSize;
  mfxU8 *surfaceBuffers;
  mfxVideoParam par;
  memset(&par, 0, sizeof(par));

  if (!convert_codec(dataFormat, mfxEncParams.mfx.CodecId))
    return NULL;

  mfxEncParams.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
  mfxEncParams.mfx.TargetKbps = kbs;
  mfxEncParams.mfx.RateControlMethod = MFX_RATECONTROL_CBR;
  mfxEncParams.mfx.FrameInfo.FourCC = MFX_FOURCC_BGR4;
  mfxEncParams.mfx.FrameInfo.FrameRateExtN = framerate;
  mfxEncParams.mfx.FrameInfo.FrameRateExtD = 1;
  mfxEncParams.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
  mfxEncParams.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
  mfxEncParams.mfx.FrameInfo.CropX = 0;
  mfxEncParams.mfx.FrameInfo.CropY = 0;
  mfxEncParams.mfx.FrameInfo.CropW = w;
  mfxEncParams.mfx.FrameInfo.CropH = h;
  // Width must be a multiple of 16
  // Height must be a multiple of 16 in case of frame picture and a multiple of
  // 32 in case of field picture
  mfxEncParams.mfx.FrameInfo.Width = MSDK_ALIGN16(w); // todo
  mfxEncParams.mfx.FrameInfo.Height =
      (MFX_PICSTRUCT_PROGRESSIVE == mfxEncParams.mfx.FrameInfo.PicStruct)
          ? MSDK_ALIGN16(h)
          : MSDK_ALIGN32(h);
  mfxEncParams.mfx.EncodedOrder = 0;

  mfxEncParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;

  // Configuration for low latency
  mfxEncParams.AsyncDepth = 1; // 1 is best for low latency
  mfxEncParams.mfx.GopRefDist =
      1; // 1 is best for low latency, I and P frames only

  MFXEncoder *p = new MFXEncoder();
  if (!p)
    goto _exit;

  p->width_ = w;
  p->height_ = h;
  p->native_ = std::make_unique<NativeDevice>();
  if (!p->native_->Init(luid, (ID3D11Device *)handle))
    goto _exit;

  sts = p->InitMFX();
  CHECK_STATUS_GOTO(sts, "InitMFX");

  // Create Media SDK encoder
  p->mfxENC_ = new MFXVideoENCODE(p->session_);
  if (!p->mfxENC_) {
    goto _exit;
  }

  // Validate video encode parameters (optional)
  // - In this example the validation result is written to same structure
  // - MFX_WRN_INCOMPATIBLE_VIDEO_PARAM is returned if some of the video
  // parameters are not supported,
  //   instead the encoder will select suitable parameters closest matching the
  //   requested configuration
  sts = p->mfxENC_->Query(&mfxEncParams, &mfxEncParams);
  MSDK_IGNORE_MFX_STS(sts, MFX_WRN_INCOMPATIBLE_VIDEO_PARAM);
  CHECK_STATUS_GOTO(sts, "Query");

  sts = p->mfxENC_->QueryIOSurf(&mfxEncParams, &EncRequest);
  CHECK_STATUS_GOTO(sts, "QueryIOSurf");

  nEncSurfNum = EncRequest.NumFrameSuggested;

  // Allocate surface headers (mfxFrameSurface1) for encoder
  p->pEncSurfaces_.resize(nEncSurfNum);
  for (int i = 0; i < nEncSurfNum; i++) {
    memset(&p->pEncSurfaces_[i], 0, sizeof(mfxFrameSurface1));
    p->pEncSurfaces_[i].Info = mfxEncParams.mfx.FrameInfo;
  }

  // Initialize the Media SDK encoder
  sts = p->mfxENC_->Init(&mfxEncParams);
  MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
  CHECK_STATUS_GOTO(sts, "Init");

  // Retrieve video parameters selected by encoder.
  // - BufferSizeInKB parameter is required to set bit stream buffer size
  sts = p->mfxENC_->GetVideoParam(&par);
  CHECK_STATUS_GOTO(sts, "GetVideoParam");

  // Prepare Media SDK bit stream buffer
  memset(&p->mfxBS_, 0, sizeof(p->mfxBS_));
  p->mfxBS_.MaxLength = par.mfx.BufferSizeInKB * 1024;
  p->bstData_.resize(p->mfxBS_.MaxLength);
  p->mfxBS_.Data = p->bstData_.data();

  return p;
_exit:
  if (p) {
    mfx_destroy_encoder(p);
    delete p;
  }
  return NULL;
}

int mfx_encode(void *encoder, ID3D11Texture2D *tex, EncodeCallback callback,
               void *obj) {
  mfxStatus sts = MFX_ERR_NONE;
  bool encoded = false;
  MFXEncoder *p = (MFXEncoder *)encoder;
  int nEncSurfIdx = 0;
  mfxSyncPoint syncp;

  p->mfxBS_.DataLength = 0;
  p->mfxBS_.DataOffset = 0;

  nEncSurfIdx =
      GetFreeSurfaceIndex(p->pEncSurfaces_.data(),
                          p->pEncSurfaces_.size()); // Find free frame surface
  if (nEncSurfIdx >= p->pEncSurfaces_.size()) {
    return -1;
  }

  p->pEncSurfaces_[nEncSurfIdx].Data.MemId = tex;

  for (;;) {
    // Encode a frame asychronously (returns immediately)
    sts = p->mfxENC_->EncodeFrameAsync(NULL, &p->pEncSurfaces_[nEncSurfIdx],
                                       &p->mfxBS_, &syncp);

    if (MFX_ERR_NONE < sts &&
        !syncp) { // Repeat the call if warning and no output
      if (MFX_WRN_DEVICE_BUSY == sts)
        MSDK_SLEEP(1); // Wait if device is busy, then repeat the same call
    } else if (MFX_ERR_NONE < sts && syncp) {
      sts = MFX_ERR_NONE; // Ignore warnings if output is available
      break;
    } else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts) {
      // Allocate more bitstream buffer memory here if needed...
      break;
    } else
      break;
  }

  if (MFX_ERR_NONE == sts) {
    sts = p->session_.SyncOperation(
        syncp, 1000); // Synchronize. Wait until encoded frame is ready
    CHECK_STATUS_RETURN(sts, "SyncOperation");
    if (p->mfxBS_.DataLength > 0) {
      int key = (p->mfxBS_.FrameType & MFX_FRAMETYPE_I) ||
                (p->mfxBS_.FrameType & MFX_FRAMETYPE_IDR);
      if (callback)
        callback(p->mfxBS_.Data + p->mfxBS_.DataOffset, p->mfxBS_.DataLength,
                 key, obj);
      encoded = true;
    }
  }

  // MFX_ERR_MORE_DATA means that the input file has ended, need to go to
  // buffering loop, exit in case of other errors
  MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
  CHECK_STATUS_RETURN(sts, "");
  return encoded ? 0 : -1;
}

int mfx_test_encode(void *outDescs, int32_t maxDescNum, int32_t *outDescNum,
                    API api, DataFormat dataFormat, int32_t width,
                    int32_t height, int32_t kbs, int32_t framerate,
                    int32_t gop) {
  try {
    AdapterDesc *descs = (AdapterDesc *)outDescs;
    Adapters adapters;
    if (!adapters.Init(ADAPTER_VENDOR_INTEL))
      return -1;
    int count = 0;
    for (auto &adapter : adapters.adapters_) {
      MFXEncoder *e = (MFXEncoder *)mfx_new_encoder(
          (void *)adapter.get()->device_.Get(), LUID(adapter.get()->desc1_),
          api, dataFormat, width, height, kbs, framerate, gop);
      if (!e)
        continue;
      if (!e->native_->EnsureTexture(e->width_, e->height_))
        continue;
      e->native_->next();
      if (mfx_encode(e, e->native_->GetCurrentTexture(), nullptr, nullptr) ==
          0) {
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

// https://github.com/Intel-Media-SDK/MediaSDK/blob/master/doc/mediasdk-man.md#dynamic-bitrate-change
// https://github.com/Intel-Media-SDK/MediaSDK/blob/master/doc/mediasdk-man.md#mfxinfomfx
int mfx_set_bitrate(void *encoder, int32_t kbs) {
  MFXEncoder *p = (MFXEncoder *)encoder;
  mfxVideoParam param = {0};
  mfxStatus sts = MFX_ERR_NONE;
  sts = MFXVideoENCODE_GetVideoParam(p->session_, &param);
  CHECK_STATUS_RETURN(sts, "MFXVideoENCODE_GetVideoParam");
  param.mfx.TargetKbps = kbs;
  sts = MFXVideoENCODE_Reset(p->session_, &param);
  CHECK_STATUS_RETURN(sts, "MFXVideoENCODE_Reset");
  return 0;
}

int mfx_set_framerate(void *encoder, int32_t framerate) { return -1; }
}
