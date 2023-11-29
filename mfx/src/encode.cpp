#include <cstring>
#include <iostream>
#include <sample_defs.h>
#include <sample_utils.h>

#include "callback.h"
#include "common.h"
#include "system.h"

#define LOG_MODULE "MFX ENCODE"
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
      LOG_ERROR(MSG + " failed, sts=" + std::to_string((int)X));               \
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

mfxStatus MFX_CDECL simple_getHDL(mfxHDL pthis, mfxMemId mid, mfxHDL *handle) {
  mfxHDLPair *pair = (mfxHDLPair *)handle;
  pair->first = mid;
  pair->second = (mfxHDL)(UINT)0;
  return MFX_ERR_NONE;
}

mfxFrameAllocator frameAllocator{{},   NULL,          NULL, NULL,
                                 NULL, simple_getHDL, NULL};

mfxStatus InitSession(MFXVideoSession &session) {
  mfxInitParam mfxparams{};
  mfxIMPL impl = MFX_IMPL_HARDWARE_ANY | MFX_IMPL_VIA_D3D11;
  mfxparams.Implementation = impl;
  mfxparams.Version.Major = 1;
  mfxparams.Version.Minor = 0;
  mfxparams.GPUCopy = MFX_GPUCOPY_OFF;

  return session.InitEx(mfxparams);
}

int set_qp(mfxExtCodingOption2 *codingOption2, int32_t q_min, int32_t q_max) {
  bool q_valid =
      q_min >= 0 && q_min <= 51 && q_max >= 0 && q_max <= 51 && q_min <= q_max;
  if (!q_valid) {
    LOG_WARN("invalid qp range: [" + std::to_string(q_min) + ", " +
             std::to_string(q_max) + "]");
    return -1;
  }
  if (!codingOption2) {
    LOG_ERROR("codingOption2 is null");
    return -1;
  }
  codingOption2->MinQPI = q_min;
  codingOption2->MinQPB = q_min;
  codingOption2->MinQPP = q_min;
  codingOption2->MaxQPI = q_max;
  codingOption2->MaxQPB = q_max;
  codingOption2->MaxQPP = q_max;
  return 0;
}
class MFXEncoder {
public:
  std::unique_ptr<NativeDevice> native_ = nullptr;
  MFXVideoSession session_;
  MFXVideoENCODE *mfxENC_ = nullptr;
  std::vector<mfxFrameSurface1> pEncSurfaces_;
  std::vector<mfxU8> bstData_;
  mfxBitstream mfxBS_;
  MfxVideoParamsWrapper mfxEncParams_;
  ComPtr<ID3D11Texture2D> nv12_texture_ = nullptr;

  void *handle_ = nullptr;
  int64_t luid_;
  API api_;
  DataFormat dataFormat_;
  int32_t width_ = 0;
  int32_t height_ = 0;
  int32_t kbs_;
  int32_t framerate_;
  int32_t gop_;
  int32_t q_min_;
  int32_t q_max_;

  MFXEncoder(void *handle, int64_t luid, API api, DataFormat dataFormat,
             int32_t width, int32_t height, int32_t kbs, int32_t framerate,
             int32_t gop, int32_t q_min, int32_t q_max) {
    handle_ = handle;
    luid_ = luid;
    api_ = api;
    dataFormat_ = dataFormat;
    width_ = width;
    height_ = height;
    kbs_ = kbs;
    framerate_ = framerate;
    gop_ = gop;
    q_min_ = q_min;
    q_max_ = q_max;
  }

  mfxStatus Init() {
    mfxStatus sts = MFX_ERR_NONE;
    mfxVersion ver = {{0, 1}};
    mfxFrameAllocRequest EncRequest;
    memset(&EncRequest, 0, sizeof(EncRequest));
    mfxU16 nEncSurfNum;
    mfxU32 surfaceSize;
    mfxU8 *surfaceBuffers;
    MfxVideoParamsWrapper retrieved_par;
    memset(&retrieved_par, 0, sizeof(retrieved_par));
    mfxExtCodingOption2 *codingOption2 = nullptr;
    bool q_valid = false;

    memset(&mfxEncParams_, 0, sizeof(mfxEncParams_));

    if (!convert_codec(dataFormat_, mfxEncParams_.mfx.CodecId)) {
      LOG_ERROR("unsupported dataFormat: " + std::to_string(dataFormat_));
      return MFX_ERR_UNSUPPORTED;
    }

    mfxEncParams_.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
    mfxEncParams_.mfx.TargetKbps = kbs_;
    mfxEncParams_.mfx.RateControlMethod = MFX_RATECONTROL_CBR;
    mfxEncParams_.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12; // MFX_FOURCC_BGR4;
    mfxEncParams_.mfx.FrameInfo.FrameRateExtN = framerate_;
    mfxEncParams_.mfx.FrameInfo.FrameRateExtD = 1;
    mfxEncParams_.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    mfxEncParams_.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    mfxEncParams_.mfx.FrameInfo.CropX = 0;
    mfxEncParams_.mfx.FrameInfo.CropY = 0;
    mfxEncParams_.mfx.FrameInfo.CropW = width_;
    mfxEncParams_.mfx.FrameInfo.CropH = height_;
    // Width must be a multiple of 16
    // Height must be a multiple of 16 in case of frame picture and a multiple
    // of 32 in case of field picture
    mfxEncParams_.mfx.FrameInfo.Width = MSDK_ALIGN16(width_); // todo
    mfxEncParams_.mfx.FrameInfo.Height =
        (MFX_PICSTRUCT_PROGRESSIVE == mfxEncParams_.mfx.FrameInfo.PicStruct)
            ? MSDK_ALIGN16(height_)
            : MSDK_ALIGN32(height_);
    mfxEncParams_.mfx.EncodedOrder = 0;

    mfxEncParams_.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;

    // Configuration for low latency
    mfxEncParams_.AsyncDepth = 1; // 1 is best for low latency
    mfxEncParams_.mfx.GopRefDist =
        1; // 1 is best for low latency, I and P frames only

    q_valid = q_min_ >= 0 && q_min_ <= 51 && q_max_ >= 0 && q_max_ <= 51 &&
              q_min_ <= q_max_;
    if (false /*q_valid*/) {
      codingOption2 = mfxEncParams_.AddExtBuffer<mfxExtCodingOption2>();
      set_qp(codingOption2, q_min_, q_max_);
    }

    native_ = std::make_unique<NativeDevice>();
    if (!native_->Init(luid_, (ID3D11Device *)handle_)) {
      return MFX_ERR_DEVICE_FAILED;
    }

    sts = InitMFX();
    CHECK_STATUS_RETURN(sts, "InitMFX");

    // Create Media SDK encoder
    mfxENC_ = new MFXVideoENCODE(session_);
    if (!mfxENC_) {
      return MFX_ERR_NOT_INITIALIZED;
    }

    // Validate video encode parameters (optional)
    // - In this example the validation result is written to same structure
    // - MFX_WRN_INCOMPATIBLE_VIDEO_PARAM is returned if some of the video
    // parameters are not supported,
    //   instead the encoder will select suitable parameters closest matching
    //   the requested configuration
    sts = mfxENC_->Query(&mfxEncParams_, &mfxEncParams_);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_INCOMPATIBLE_VIDEO_PARAM);
    CHECK_STATUS_RETURN(sts, "Query");

    sts = mfxENC_->QueryIOSurf(&mfxEncParams_, &EncRequest);
    CHECK_STATUS_RETURN(sts, "QueryIOSurf");

    nEncSurfNum = EncRequest.NumFrameSuggested;

    // Allocate surface headers (mfxFrameSurface1) for encoder
    pEncSurfaces_.resize(nEncSurfNum);
    for (int i = 0; i < nEncSurfNum; i++) {
      memset(&pEncSurfaces_[i], 0, sizeof(mfxFrameSurface1));
      pEncSurfaces_[i].Info = mfxEncParams_.mfx.FrameInfo;
    }

    // Initialize the Media SDK encoder
    sts = mfxENC_->Init(&mfxEncParams_);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    CHECK_STATUS_RETURN(sts, "Init");

    // Retrieve video parameters selected by encoder.
    // - BufferSizeInKB parameter is required to set bit stream buffer size
    sts = mfxENC_->GetVideoParam(&retrieved_par);
    CHECK_STATUS_RETURN(sts, "GetVideoParam");

    // Prepare Media SDK bit stream buffer
    memset(&mfxBS_, 0, sizeof(mfxBS_));
    mfxBS_.MaxLength = retrieved_par.mfx.BufferSizeInKB * 1024;
    bstData_.resize(mfxBS_.MaxLength);
    mfxBS_.Data = bstData_.data();
    return MFX_ERR_NONE;
  }

  int encode(ID3D11Texture2D *tex, EncodeCallback callback, void *obj) {
    mfxStatus sts = MFX_ERR_NONE;
    bool encoded = false;
    int nEncSurfIdx = 0;
    mfxSyncPoint syncp;

    // bgra -> nv12
    if (!nv12_texture_) {
      D3D11_TEXTURE2D_DESC desc;
      ZeroMemory(&desc, sizeof(desc));
      tex->GetDesc(&desc);
      desc.Format = DXGI_FORMAT_NV12;
      desc.MiscFlags = 0;
      HRI(native_->device_->CreateTexture2D(
          &desc, NULL, nv12_texture_.ReleaseAndGetAddressOf()));
    }
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc;
    ZeroMemory(&contentDesc, sizeof(contentDesc));
    contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    contentDesc.InputFrameRate.Numerator = framerate_;
    contentDesc.InputFrameRate.Denominator = 1;
    contentDesc.InputWidth = width_;
    contentDesc.InputHeight = height_;
    contentDesc.OutputWidth = width_;
    contentDesc.OutputHeight = height_;
    contentDesc.OutputFrameRate.Numerator = framerate_;
    contentDesc.OutputFrameRate.Denominator = 1;
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE colorSpace;
    ZeroMemory(&colorSpace, sizeof(colorSpace));
    colorSpace.YCbCr_Matrix = 0; // 0:601, 1:709
    colorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
    if (!native_->Process(tex, nv12_texture_.Get(), contentDesc, colorSpace)) {
      LOG_ERROR("Failed to process");
      return -1;
    }

    mfxBS_.DataLength = 0;
    mfxBS_.DataOffset = 0;

    nEncSurfIdx =
        GetFreeSurfaceIndex(pEncSurfaces_.data(),
                            pEncSurfaces_.size()); // Find free frame surface
    if (nEncSurfIdx >= pEncSurfaces_.size()) {
      return -1;
    }

    pEncSurfaces_[nEncSurfIdx].Data.MemId = nv12_texture_.Get(); // tex;

    for (;;) {
      // Encode a frame asychronously (returns immediately)
      sts = mfxENC_->EncodeFrameAsync(NULL, &pEncSurfaces_[nEncSurfIdx],
                                      &mfxBS_, &syncp);

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
      sts = session_.SyncOperation(
          syncp, 1000); // Synchronize. Wait until encoded frame is ready
      CHECK_STATUS_RETURN(sts, "SyncOperation");
      if (mfxBS_.DataLength > 0) {
        int key = (mfxBS_.FrameType & MFX_FRAMETYPE_I) ||
                  (mfxBS_.FrameType & MFX_FRAMETYPE_IDR);
        if (callback)
          callback(mfxBS_.Data + mfxBS_.DataOffset, mfxBS_.DataLength, key,
                   obj);
        encoded = true;
      }
    }

    // MFX_ERR_MORE_DATA means that the input file has ended, need to go to
    // buffering loop, exit in case of other errors
    MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
    CHECK_STATUS_RETURN(sts, "");
    return encoded ? 0 : -1;
  }

private:
  mfxStatus InitMFX() {
    mfxStatus sts = MFX_ERR_NONE;

    sts = InitSession(session_);
    LOG_MSDK_CHECK_STATUS(sts, "InitSession");
    sts = session_.SetHandle(MFX_HANDLE_D3D11_DEVICE, native_->device_.Get());
    LOG_MSDK_CHECK_STATUS(sts, "SetHandle");
    sts = session_.SetFrameAllocator(&frameAllocator);
    LOG_MSDK_CHECK_STATUS(sts, "SetFrameAllocator");

    return MFX_ERR_NONE;
  }

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
};

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
                      int32_t framerate, int32_t gop, int32_t q_min,
                      int32_t q_max) {
  MFXEncoder *p = NULL;
  try {
    p = new MFXEncoder(handle, luid, api, dataFormat, w, h, kbs, framerate, gop,
                       q_min, q_max);
    if (!p) {
      return NULL;
    }
    mfxStatus sts = p->Init();
    if (sts == MFX_ERR_NONE) {
      return p;
    } else {
      LOG_ERROR("Init failed, sts=" + std::to_string(sts));
    }
  } catch (const std::exception &e) {
    LOG_ERROR("Exception: " + e.what());
  }

  if (p) {
    mfx_destroy_encoder(p);
    delete p;
  }
  return NULL;
}

int mfx_encode(void *encoder, ID3D11Texture2D *tex, EncodeCallback callback,
               void *obj) {
  try {
    return ((MFXEncoder *)encoder)->encode(tex, callback, obj);
  } catch (const std::exception &e) {
    LOG_ERROR("Exception: " + e.what());
  }
  return -1;
}

int mfx_test_encode(void *outDescs, int32_t maxDescNum, int32_t *outDescNum,
                    API api, DataFormat dataFormat, int32_t width,
                    int32_t height, int32_t kbs, int32_t framerate, int32_t gop,
                    int32_t q_min, int32_t q_max) {
  try {
    AdapterDesc *descs = (AdapterDesc *)outDescs;
    Adapters adapters;
    if (!adapters.Init(ADAPTER_VENDOR_INTEL))
      return -1;
    int count = 0;
    for (auto &adapter : adapters.adapters_) {
      MFXEncoder *e = (MFXEncoder *)mfx_new_encoder(
          (void *)adapter.get()->device_.Get(), LUID(adapter.get()->desc1_),
          api, dataFormat, width, height, kbs, framerate, gop, q_min, q_max);
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
    LOG_ERROR("test failed: " + e.what());
  }
  return -1;
}

// https://github.com/Intel-Media-SDK/MediaSDK/blob/master/doc/mediasdk-man.md#dynamic-bitrate-change
// https://github.com/Intel-Media-SDK/MediaSDK/blob/master/doc/mediasdk-man.md#mfxinfomfx
int mfx_set_bitrate(void *encoder, int32_t kbs) {
  try {
    MFXEncoder *p = (MFXEncoder *)encoder;
    mfxStatus sts = MFX_ERR_NONE;
    p->mfxEncParams_.mfx.TargetKbps = kbs;
    sts = MFXVideoENCODE_Reset(p->session_, &p->mfxEncParams_);
    CHECK_STATUS_RETURN(sts, "MFXVideoENCODE_Reset");
    return 0;
  } catch (const std::exception &e) {
    LOG_ERROR("Exception: " + e.what());
  }
  return -1;
}

int mfx_set_qp(void *encoder, int32_t q_min, int32_t q_max) {
  /*
  MFXEncoder *p = (MFXEncoder *)encoder;
  mfxStatus sts = MFX_ERR_NONE;

  mfxExtCodingOption2 *codingOption2 =
      p->mfxEncParams_.GetExtBuffer<mfxExtCodingOption2>();
  if (codingOption2 != nullptr) {
    if (set_qp(codingOption2, q_min, q_max) != 0) {
      return -1;
    }
    sts = MFXVideoENCODE_Reset(p->session_, &p->mfxEncParams_);
    CHECK_STATUS_RETURN(sts, "MFXVideoENCODE_Reset");
    return 0;
  } else {
    LOG_ERROR("codingOption2 is null");
    return -1;
  }
  */
  return -1;
}

int mfx_set_framerate(void *encoder, int32_t framerate) {
  LOG_WARN("not support change framerate");
  return -1;
}
}
