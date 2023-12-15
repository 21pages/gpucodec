#include <cstring>
#include <iostream>
#include <sample_defs.h>
#include <sample_utils.h>

#include "callback.h"
#include "common.h"
#include "system.h"

#define LOG_MODULE "MFX ENCODE"
#include "log.h"

// #define CONFIG_USE_VPP
// #define CONFIG_USE_D3D_CONVERT

#define CHECK_STATUS(X, MSG)                                                   \
  {                                                                            \
    mfxStatus __sts = (X);                                                     \
    if (__sts < MFX_ERR_NONE) {                                                \
      MSDK_PRINT_RET_MSG(__sts, MSG);                                          \
      LOG_ERROR(MSG + " failed, sts=" + std::to_string((int)__sts));           \
      return __sts;                                                            \
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
  std::vector<mfxFrameSurface1> encSurfaces_;
  std::vector<mfxU8> bstData_;
  mfxBitstream mfxBS_;
  mfxVideoParam mfxEncParams_;
  mfxExtBuffer *extbuffers_[2] = {NULL, NULL};
  mfxExtVideoSignalInfo signal_info_;
  mfxExtCodingOption2 option2_;

// vpp
#ifdef CONFIG_USE_VPP
  MFXVideoVPP *mfxVPP_ = nullptr;
  mfxVideoParam vppParams_;
  mfxExtBuffer *vppExtBuffers_[1] = {NULL};
  mfxExtVPPDoNotUse vppDontUse_;
  mfxU32 vppDontUseArgList_[4];
  std::vector<mfxFrameSurface1> vppSurfaces_;
#endif

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

  bool full_range_ = false;
  bool bt709_ = false;

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

    native_ = std::make_unique<NativeDevice>();
    if (!native_->Init(luid_, (ID3D11Device *)handle_)) {
      LOG_ERROR("failed to init native device");
      return MFX_ERR_DEVICE_FAILED;
    }
    sts = initMFX();
    CHECK_STATUS(sts, "initMFX");
#ifdef CONFIG_USE_VPP
    sts = initVpp();
    CHECK_STATUS(sts, "initVpp");
#endif
    sts = initEnc();
    CHECK_STATUS(sts, "initEnc");
    return MFX_ERR_NONE;
  }

  int encode(ID3D11Texture2D *tex, EncodeCallback callback, void *obj) {
    mfxStatus sts = MFX_ERR_NONE;

    int nEncSurfIdx =
        GetFreeSurfaceIndex(encSurfaces_.data(), encSurfaces_.size());
    if (nEncSurfIdx >= encSurfaces_.size()) {
      LOG_ERROR("no free enc surface");
      return -1;
    }
    mfxFrameSurface1 *encSurf = &encSurfaces_[nEncSurfIdx];
#ifdef CONFIG_USE_VPP
    mfxSyncPoint syncp;
    sts = vppOneFrame(tex, encSurf, syncp);
    syncp = NULL;
    if (sts != MFX_ERR_NONE) {
      LOG_ERROR("vppOneFrame failed, sts=" + std::to_string((int)sts));
      return -1;
    }
#elif defined(CONFIG_USE_D3D_CONVERT)
    if (!native_->ToNV12(tex, width_, height_, bt709_, full_range_)) {
      LOG_ERROR("failed to convert to NV12");
      return -1;
    }
    encSurf->Data.MemId = native_->nv12_texture_.Get();
#else
    encSurf->Data.MemId = tex;
#endif
    return encodeOneFrame(encSurf, callback, obj);
  }

private:
  mfxStatus initMFX() {
    mfxStatus sts = MFX_ERR_NONE;

    sts = InitSession(session_);
    CHECK_STATUS(sts, "InitSession");
    sts = session_.SetHandle(MFX_HANDLE_D3D11_DEVICE, native_->device_.Get());
    CHECK_STATUS(sts, "SetHandle");
    sts = session_.SetFrameAllocator(&frameAllocator);
    CHECK_STATUS(sts, "SetFrameAllocator");

    return MFX_ERR_NONE;
  }

#ifdef CONFIG_USE_VPP
  mfxStatus initVpp() {
    mfxStatus sts = MFX_ERR_NONE;
    memset(&vppParams_, 0, sizeof(vppParams_));
    vppParams_.IOPattern =
        MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;
    vppParams_.vpp.In.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    vppParams_.vpp.In.FrameRateExtN = framerate_;
    vppParams_.vpp.In.FrameRateExtD = 1;
    vppParams_.vpp.In.Width = MSDK_ALIGN16(width_);
    vppParams_.vpp.In.Height =
        (MFX_PICSTRUCT_PROGRESSIVE == vppParams_.vpp.In.PicStruct)
            ? MSDK_ALIGN16(height_)
            : MSDK_ALIGN32(height_);
    vppParams_.vpp.In.CropX = 0;
    vppParams_.vpp.In.CropY = 0;
    vppParams_.vpp.In.CropW = width_;
    vppParams_.vpp.In.CropH = height_;
    vppParams_.vpp.In.Shift = 0;
    memcpy(&vppParams_.vpp.Out, &vppParams_.vpp.In, sizeof(vppParams_.vpp.Out));
    vppParams_.vpp.In.FourCC = MFX_FOURCC_RGB4;
    vppParams_.vpp.Out.FourCC = MFX_FOURCC_NV12;
    vppParams_.vpp.In.ChromaFormat = MFX_CHROMAFORMAT_YUV444;
    vppParams_.vpp.Out.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    vppParams_.AsyncDepth = 1;

    vppParams_.ExtParam = vppExtBuffers_;
    vppParams_.NumExtParam = 1;
    vppExtBuffers_[0] = (mfxExtBuffer *)&vppDontUse_;
    vppDontUse_.Header.BufferId = MFX_EXTBUFF_VPP_DONOTUSE;
    vppDontUse_.Header.BufferSz = sizeof(vppDontUse_);
    vppDontUse_.AlgList = vppDontUseArgList_;
    vppDontUse_.NumAlg = 4;
    vppDontUseArgList_[0] = MFX_EXTBUFF_VPP_DENOISE;
    vppDontUseArgList_[1] = MFX_EXTBUFF_VPP_SCENE_ANALYSIS;
    vppDontUseArgList_[2] = MFX_EXTBUFF_VPP_DETAIL;
    vppDontUseArgList_[3] = MFX_EXTBUFF_VPP_PROCAMP;

    mfxVPP_ = new MFXVideoVPP(session_);
    if (!mfxVPP_) {
      LOG_ERROR("Failed to create MFXVideoVPP");
      return MFX_ERR_MEMORY_ALLOC;
    }

    sts = mfxVPP_->Query(&vppParams_, &vppParams_);
    CHECK_STATUS(sts, "vpp query");
    mfxFrameAllocRequest vppAllocRequest;
    ZeroMemory(&vppAllocRequest, sizeof(vppAllocRequest));
    memcpy(&vppAllocRequest.Info, &vppParams_.vpp.In, sizeof(mfxFrameInfo));
    sts = mfxVPP_->QueryIOSurf(&vppParams_, &vppAllocRequest);
    CHECK_STATUS(sts, "vpp QueryIOSurf");

    vppSurfaces_.resize(vppAllocRequest.NumFrameSuggested);
    for (int i = 0; i < vppAllocRequest.NumFrameSuggested; i++) {
      memset(&vppSurfaces_[i], 0, sizeof(mfxFrameSurface1));
      memcpy(&vppSurfaces_[i].Info, &vppParams_.vpp.In, sizeof(mfxFrameInfo));
    }

    sts = mfxVPP_->Init(&vppParams_);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    CHECK_STATUS(sts, "vpp init");

    return MFX_ERR_NONE;
  }
#endif

  mfxStatus initEnc() {
    mfxStatus sts = MFX_ERR_NONE;
    memset(&mfxEncParams_, 0, sizeof(mfxEncParams_));
    if (!convert_codec(dataFormat_, mfxEncParams_.mfx.CodecId)) {
      LOG_ERROR("unsupported dataFormat: " + std::to_string(dataFormat_));
      return MFX_ERR_UNSUPPORTED;
    }

    mfxEncParams_.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
    mfxEncParams_.mfx.TargetKbps = kbs_;
    mfxEncParams_.mfx.RateControlMethod = MFX_RATECONTROL_CBR;
    mfxEncParams_.mfx.FrameInfo.FrameRateExtN = framerate_;
    mfxEncParams_.mfx.FrameInfo.FrameRateExtD = 1;
#ifdef CONFIG_USE_VPP
    mfxEncParams_.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    mfxEncParams_.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
#elif defined(CONFIG_USE_D3D_CONVERT)
    mfxEncParams_.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    mfxEncParams_.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
#else
    mfxEncParams_.mfx.FrameInfo.FourCC = MFX_FOURCC_BGR4;
    mfxEncParams_.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV444;
#endif
    mfxEncParams_.mfx.FrameInfo.BitDepthLuma = 8;
    mfxEncParams_.mfx.FrameInfo.BitDepthChroma = 8;
    mfxEncParams_.mfx.FrameInfo.Shift = 0;
    mfxEncParams_.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    mfxEncParams_.mfx.FrameInfo.CropX = 0;
    mfxEncParams_.mfx.FrameInfo.CropY = 0;
    mfxEncParams_.mfx.FrameInfo.CropW = width_;
    mfxEncParams_.mfx.FrameInfo.CropH = height_;
    // Width must be a multiple of 16
    // Height must be a multiple of 16 in case of frame picture and a multiple
    // of 32 in case of field picture
    mfxEncParams_.mfx.FrameInfo.Width = MSDK_ALIGN16(width_);
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

    InitEncExtParams();

    // Create Media SDK encoder
    mfxENC_ = new MFXVideoENCODE(session_);
    if (!mfxENC_) {
      LOG_ERROR("failed to create MFXVideoENCODE");
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
    CHECK_STATUS(sts, "Query");

    mfxFrameAllocRequest EncRequest;
    memset(&EncRequest, 0, sizeof(EncRequest));
    sts = mfxENC_->QueryIOSurf(&mfxEncParams_, &EncRequest);
    CHECK_STATUS(sts, "QueryIOSurf");

    // Allocate surface headers (mfxFrameSurface1) for encoder
    encSurfaces_.resize(EncRequest.NumFrameSuggested);
    for (int i = 0; i < EncRequest.NumFrameSuggested; i++) {
      memset(&encSurfaces_[i], 0, sizeof(mfxFrameSurface1));
      memcpy(&encSurfaces_[i].Info, &mfxEncParams_.mfx.FrameInfo,
             sizeof(mfxFrameInfo));
    }

    // Initialize the Media SDK encoder
    sts = mfxENC_->Init(&mfxEncParams_);
    MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
    CHECK_STATUS(sts, "Init");

    // Retrieve video parameters selected by encoder.
    // - BufferSizeInKB parameter is required to set bit stream buffer size
    sts = mfxENC_->GetVideoParam(&mfxEncParams_);
    CHECK_STATUS(sts, "GetVideoParam");

    // Prepare Media SDK bit stream buffer
    memset(&mfxBS_, 0, sizeof(mfxBS_));
    mfxBS_.MaxLength = mfxEncParams_.mfx.BufferSizeInKB * 1024;
    bstData_.resize(mfxBS_.MaxLength);
    mfxBS_.Data = bstData_.data();

    return MFX_ERR_NONE;
  }

#ifdef CONFIG_USE_VPP
  mfxStatus vppOneFrame(void *texture, mfxFrameSurface1 *out,
                        mfxSyncPoint syncp) {
    mfxStatus sts = MFX_ERR_NONE;

    int surfIdx =
        GetFreeSurfaceIndex(vppSurfaces_.data(),
                            vppSurfaces_.size()); // Find free frame surface
    if (surfIdx >= vppSurfaces_.size()) {
      LOG_ERROR("No free vpp surface");
      return MFX_ERR_MORE_SURFACE;
    }
    mfxFrameSurface1 *in = &vppSurfaces_[surfIdx];
    in->Data.MemId = texture;

    for (;;) {
      sts = mfxVPP_->RunFrameVPPAsync(in, out, NULL, &syncp);

      if (MFX_ERR_NONE < sts &&
          !syncp) // repeat the call if warning and no output
      {
        if (MFX_WRN_DEVICE_BUSY == sts)
          MSDK_SLEEP(1); // wait if device is busy
      } else if (MFX_ERR_NONE < sts && syncp) {
        sts = MFX_ERR_NONE; // ignore warnings if output is available
        break;
      } else {
        break; // not a warning
      }
    }

    if (MFX_ERR_NONE == sts) {
      sts = session_.SyncOperation(
          syncp, 1000); // Synchronize. Wait until encoded frame is ready
      CHECK_STATUS(sts, "SyncOperation");
    }

    return sts;
  }
#endif

  int encodeOneFrame(mfxFrameSurface1 *in, EncodeCallback callback, void *obj) {
    mfxStatus sts = MFX_ERR_NONE;
    mfxSyncPoint syncp;
    bool encoded = false;

    mfxBS_.DataLength = 0;
    mfxBS_.DataOffset = 0;
    for (;;) {
      // Encode a frame asychronously (returns immediately)
      sts = mfxENC_->EncodeFrameAsync(NULL, in, &mfxBS_, &syncp);
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
      CHECK_STATUS(sts, "SyncOperation");
      if (mfxBS_.DataLength > 0) {
        int key = (mfxBS_.FrameType & MFX_FRAMETYPE_I) ||
                  (mfxBS_.FrameType & MFX_FRAMETYPE_IDR);
        if (callback)
          callback(mfxBS_.Data + mfxBS_.DataOffset, mfxBS_.DataLength, key,
                   obj);
        encoded = true;
      }
    }

    return encoded ? 0 : -1;
  }

  void InitEncExtParams() {
    memset(&signal_info_, 0, sizeof(mfxExtVideoSignalInfo));
    memset(&option2_, 0, sizeof(mfxExtCodingOption2));
    signal_info_.Header.BufferId = MFX_EXTBUFF_VIDEO_SIGNAL_INFO;
    signal_info_.Header.BufferSz = sizeof(mfxExtVideoSignalInfo);
    option2_.Header.BufferId = MFX_EXTBUFF_CODING_OPTION2;
    option2_.Header.BufferSz = sizeof(mfxExtCodingOption2);

    // https://github.com/GStreamer/gstreamer/blob/651dcb49123ec516e7c582e4a49a5f3f15c10f93/subprojects/gst-plugins-bad/sys/qsv/gstqsvh264enc.cpp#L1647
    extbuffers_[0] = (mfxExtBuffer *)&signal_info_;
    extbuffers_[1] = (mfxExtBuffer *)&option2_;
    mfxEncParams_.ExtParam = extbuffers_;
    mfxEncParams_.NumExtParam = 2;

    signal_info_.VideoFormat = 5;
    signal_info_.ColourDescriptionPresent = 1;
    signal_info_.VideoFullRange = !!full_range_;
    signal_info_.MatrixCoefficients =
        bt709_ ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
    signal_info_.ColourPrimaries =
        bt709_ ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M;
    signal_info_.TransferCharacteristics =
        bt709_ ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M;

    set_qp(&option2_, q_min_, q_max_);
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
#ifdef CONFIG_USE_VPP
    if (p->mfxVPP_) {
      p->mfxVPP_->Close();
      delete p->mfxVPP_;
    }
#endif
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
    CHECK_STATUS(sts, "MFXVideoENCODE_Reset");
    return 0;
  } catch (const std::exception &e) {
    LOG_ERROR("Exception: " + e.what());
  }
  return -1;
}

int mfx_set_qp(void *encoder, int32_t q_min, int32_t q_max) {
  try {
    MFXEncoder *p = (MFXEncoder *)encoder;
    mfxStatus sts = MFX_ERR_NONE;

    if (set_qp(&p->option2_, q_min, q_max) != 0) {
      return -1;
    }
    // TODO: whether works
    sts = MFXVideoENCODE_Reset(p->session_, &p->mfxEncParams_);
    CHECK_STATUS(sts, "MFXVideoENCODE_Reset");
    return 0;
  } catch (const std::exception &e) {
    LOG_ERROR("mfx_set_qp: " + e.what());
  }
  return -1;
}

int mfx_set_framerate(void *encoder, int32_t framerate) {
  LOG_WARN("not support change framerate");
  return -1;
}
}
