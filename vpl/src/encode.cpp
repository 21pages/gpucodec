#include <cstring>
#include <iostream>

#include "callback.h"
#include "common.h"
#include "system.h"

#define LOG_MODULE "VPL ENCODE"
#include "log.h"

#define CONFIG_USE_D3D_CONVERT

namespace {

#include <api1x_core\legacy-encode\src\util.hpp>

class VplEncoder {
public:
  mfxLoader loader_ = NULL;
  int accel_fd_ = 0;
  void *accelHandle_ = NULL;
  mfxConfig cfg_[1];
  mfxVariant cfgVal_[1];
  mfxSession session_ = NULL;
  mfxVideoParam encodeParams_ = {};
  std::vector<mfxFrameSurface1> encSurfaces_;
  std::vector<mfxU8> bstData_;
  mfxBitstream mfxBS_;

  std::unique_ptr<NativeDevice> native_ = nullptr;

  mfxVideoParam mfxEncParams_;
  mfxExtBuffer *extbuffers_[1] = {NULL};
  mfxExtVideoSignalInfo signal_info_;

  void *handle_ = nullptr;
  int64_t luid_;
  API api_;
  DataFormat dataFormat_;
  int32_t width_ = 0;
  int32_t height_ = 0;
  int32_t kbs_;
  int32_t framerate_;
  int32_t gop_;

  bool full_range_ = false;
  bool bt709_ = false;

  VplEncoder(void *handle, int64_t luid, API api, DataFormat dataFormat,
             int32_t width, int32_t height, int32_t kbs, int32_t framerate,
             int32_t gop) {
    handle_ = handle;
    luid_ = luid;
    api_ = api;
    dataFormat_ = dataFormat;
    width_ = width;
    height_ = height;
    kbs_ = kbs;
    framerate_ = framerate;
    gop_ = gop;
  }

  bool Init() {
    native_ = std::make_unique<NativeDevice>();
    if (!native_->Init(luid_, (ID3D11Device *)handle_)) {
      LOG_ERROR("failed to init native device");
      return false;
    }
    if (!initMFX()) {
      LOG_ERROR("failed to init MFX");
      return false;
    }
    if (!initEnc()) {
      LOG_ERROR("failed to init enc");
      return false;
    }
    return true;
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
#ifdef CONFIG_USE_D3D_CONVERT
    DXGI_COLOR_SPACE_TYPE colorSpace_in =
        DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    DXGI_COLOR_SPACE_TYPE colorSpace_out;
    if (bt709_) {
      if (full_range_) {
        colorSpace_out = DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709;
      } else {
        colorSpace_out = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
      }
    } else {
      if (full_range_) {
        colorSpace_out = DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601;
      } else {
        colorSpace_out = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
      }
    }
    if (!native_->ToNV12(tex, width_, height_, colorSpace_in, colorSpace_out)) {
      LOG_ERROR("failed to convert to NV12");
      return -1;
    }
    encSurf->Data.MemId = native_->nv12_texture_.Get();
#else
    encSurf->Data.MemId = tex;
#endif
    return encodeOneFrame(encSurf, callback, obj);
  }

  void destroy() {
    // Clean up resources - It is recommended to close components first, before
    // releasing allocated surfaces, since some surfaces may still be locked by
    // internal resources.
    if (session_) {
      MFXVideoENCODE_Close(session_);
      MFXClose(session_);
    }

    FreeAcceleratorHandle(accelHandle_, accel_fd_);
    accelHandle_ = NULL;
    accel_fd_ = 0;

    if (loader_)
      MFXUnload(loader_);
  }

private:
  bool initMFX() {
    loader_ = MFXLoad();
    if (!loader_) {
      LOG_ERROR("failed to load MFX");
      return false;
    }
    cfg_[0] = MFXCreateConfig(loader_);
    if (!cfg_[0]) {
      LOG_ERROR("MFXCreateConfig failed");
      return false;
    }
    cfgVal_[0].Type = MFX_VARIANT_TYPE_U32;
    cfgVal_[0].Data.U32 = MFX_IMPL_TYPE_HARDWARE;

    mfxStatus sts = MFXSetConfigFilterProperty(
        cfg_[0], (mfxU8 *)"mfxImplDescription.Impl", cfgVal_[0]);
    if (sts != MFX_ERR_NONE) {
      LOG_ERROR("MFXSetConfigFilterProperty failed for Impl, sts=" +
                std::to_string(sts));
      return false;
    }
    sts = MFXCreateSession(loader_, 0, &session_);
    if (sts != MFX_ERR_NONE) {
      LOG_ERROR("MFXCreateSession failed, sts=" + std::to_string(sts));
      return false;
    }

    // Print info about implementation loaded
    ShowImplementationInfo(loader_, 0);

    // Convenience function to initialize available accelerator(s)
    accelHandle_ = InitAcceleratorHandle(session_, &accel_fd_);

    return true;
  }

  bool initEnc() {
    mfxStatus sts = MFX_ERR_NONE;
    // memset(&mfxEncParams_, 0, sizeof(mfxEncParams_));
    if (!convert_codec(dataFormat_, mfxEncParams_.mfx.CodecId)) {
      LOG_ERROR("unsupported dataFormat: " + std::to_string(dataFormat_));
      return false;
    }

    mfxEncParams_.mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED;
    mfxEncParams_.mfx.TargetKbps = 4000; // kbs_;
    mfxEncParams_.mfx.RateControlMethod = MFX_RATECONTROL_VBR;
    mfxEncParams_.mfx.FrameInfo.FrameRateExtN = 30; // framerate_;
    mfxEncParams_.mfx.FrameInfo.FrameRateExtD = 1;
#ifdef CONFIG_USE_D3D_CONVERT
    mfxEncParams_.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    mfxEncParams_.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
#else
    mfxEncParams_.mfx.FrameInfo.FourCC = MFX_FOURCC_BGR4;
    mfxEncParams_.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV444;
#endif
    // mfxEncParams_.mfx.FrameInfo.BitDepthLuma = 8;
    // mfxEncParams_.mfx.FrameInfo.BitDepthChroma = 8;
    // mfxEncParams_.mfx.FrameInfo.Shift = 0;
    // mfxEncParams_.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    mfxEncParams_.mfx.FrameInfo.CropX = 0;
    mfxEncParams_.mfx.FrameInfo.CropY = 0;
    mfxEncParams_.mfx.FrameInfo.CropW = width_;
    mfxEncParams_.mfx.FrameInfo.CropH = height_;
    // Width must be a multiple of 16
    // Height must be a multiple of 16 in case of frame picture and a multiple
    // of 32 in case of field picture
    mfxEncParams_.mfx.FrameInfo.Width = ALIGN16(width_);
    mfxEncParams_.mfx.FrameInfo.Height =
        (MFX_PICSTRUCT_PROGRESSIVE == mfxEncParams_.mfx.FrameInfo.PicStruct)
            ? ALIGN16(height_)
            : ALIGN32(height_);
    // mfxEncParams_.mfx.EncodedOrder = 0;

    mfxEncParams_.IOPattern =
        MFX_IOPATTERN_IN_SYSTEM_MEMORY; // MFX_IOPATTERN_IN_VIDEO_MEMORY;

    // // Configuration for low latency
    // mfxEncParams_.AsyncDepth = 1; // 1 is best for low latency
    // mfxEncParams_.mfx.GopRefDist =
    //     1; // 1 is best for low latency, I and P frames only

    // InitEncExtParams();

    // Validate video encode parameters
    // - In this example the validation result is written to same structure
    // - MFX_WRN_INCOMPATIBLE_VIDEO_PARAM is returned if some of the video
    // parameters are not supported,
    //   instead the encoder will select suitable parameters closest matching
    //   the requested configuration, and it's ignorable.
    sts = MFXVideoENCODE_Query(session_, &encodeParams_, &encodeParams_);
    if (sts == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM)
      sts = MFX_ERR_NONE;
    if (sts != MFX_ERR_NONE) {
      LOG_ERROR("Encode Query failed, sts=" + std::to_string(sts));
      return false;
    }
    // Initialize encoder
    sts = MFXVideoENCODE_Init(session_, &encodeParams_);
    if (sts != MFX_ERR_NONE) {
      LOG_ERROR("Encode init failed, sts=" + std::to_string(sts));
      return false;
    }
    // Query number required surfaces for decoder
    mfxFrameAllocRequest encRequest = {};
    sts = MFXVideoENCODE_QueryIOSurf(session_, &encodeParams_, &encRequest);
    if (sts != MFX_ERR_NONE) {
      LOG_ERROR("Encode QueryIOSurf failed, sts=" + std::to_string(sts));
      return false;
    }
    // Allocate surface headers (mfxFrameSurface1) for encoder
    encSurfaces_.resize(encRequest.NumFrameSuggested);
    for (int i = 0; i < encRequest.NumFrameSuggested; i++) {
      memset(&encSurfaces_[i], 0, sizeof(mfxFrameSurface1));
      memcpy(&encSurfaces_[i].Info, &mfxEncParams_.mfx.FrameInfo,
             sizeof(mfxFrameInfo));
    }

    // Prepare Media SDK bit stream buffer
    memset(&mfxBS_, 0, sizeof(mfxBS_));
    mfxBS_.MaxLength = mfxEncParams_.mfx.BufferSizeInKB * 1024;
    bstData_.resize(mfxBS_.MaxLength);
    mfxBS_.Data = bstData_.data();

    return true;
  }

  int encodeOneFrame(mfxFrameSurface1 *in, EncodeCallback callback, void *obj) {
    mfxStatus sts = MFX_ERR_NONE;
    mfxSyncPoint syncp;
    bool encoded = false;

    mfxBS_.DataLength = 0;
    mfxBS_.DataOffset = 0;
    for (;;) {
      // Encode a frame asychronously (returns immediately)
      sts =
          MFXVideoENCODE_EncodeFrameAsync(session_, NULL, in, &mfxBS_, &syncp);
      if (MFX_ERR_NONE < sts &&
          !syncp) { // Repeat the call if warning and no output
        if (MFX_WRN_DEVICE_BUSY == sts)
          Sleep(1); // Wait if device is busy, then repeat the same call
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
      sts = MFXVideoCORE_SyncOperation(
          session_, syncp,
          1000); // Synchronize. Wait until encoded frame is ready
      if (sts != MFX_ERR_NONE) {
        LOG_ERROR("SyncOperation failed, sts=" + std::to_string(sts));
        return -1;
      }
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
    signal_info_.Header.BufferId = MFX_EXTBUFF_VIDEO_SIGNAL_INFO;
    signal_info_.Header.BufferSz = sizeof(mfxExtVideoSignalInfo);

    // https://github.com/GStreamer/gstreamer/blob/651dcb49123ec516e7c582e4a49a5f3f15c10f93/subprojects/gst-plugins-bad/sys/qsv/gstqsvh264enc.cpp#L1647
    extbuffers_[0] = (mfxExtBuffer *)&signal_info_;
    mfxEncParams_.ExtParam = extbuffers_;
    mfxEncParams_.NumExtParam = 1;

    signal_info_.VideoFormat = 5;
    signal_info_.ColourDescriptionPresent = 1;
    signal_info_.VideoFullRange = !!full_range_;
    signal_info_.MatrixCoefficients =
        bt709_ ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
    signal_info_.ColourPrimaries =
        bt709_ ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M;
    signal_info_.TransferCharacteristics =
        bt709_ ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M;
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

int driver_support() {
  mfxLoader loader = MFXLoad();
  if (loader) {
    MFXUnload(loader);
    return 0;
  } else {
    return -1;
  }
}

} // namespace

extern "C" {

int vpl_driver_support() { return driver_support(); }

int vpl_destroy_encoder(void *encoder) {
  VplEncoder *p = (VplEncoder *)encoder;
  if (p) {
    p->destroy();
  }
  return 0;
}

void *vpl_new_encoder(void *handle, int64_t luid, API api,
                      DataFormat dataFormat, int32_t w, int32_t h, int32_t kbs,
                      int32_t framerate, int32_t gop) {
  VplEncoder *p = NULL;
  try {
    p = new VplEncoder(handle, luid, api, dataFormat, w, h, kbs, framerate,
                       gop);
    if (!p) {
      return NULL;
    }
    if (p->Init()) {
      return p;
    } else {
      LOG_ERROR("Init failed");
    }
  } catch (const std::exception &e) {
    LOG_ERROR("Exception: " + e.what());
  }

  if (p) {
    vpl_destroy_encoder(p);
    delete p;
  }
  return NULL;
}

int vpl_encode(void *encoder, ID3D11Texture2D *tex, EncodeCallback callback,
               void *obj) {
  try {
    return ((VplEncoder *)encoder)->encode(tex, callback, obj);
  } catch (const std::exception &e) {
    LOG_ERROR("Exception: " + e.what());
  }
  return -1;
}

int vpl_test_encode(void *outDescs, int32_t maxDescNum, int32_t *outDescNum,
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
      VplEncoder *e = (VplEncoder *)vpl_new_encoder(
          (void *)adapter.get()->device_.Get(), LUID(adapter.get()->desc1_),
          api, dataFormat, width, height, kbs, framerate, gop);
      if (!e)
        continue;
      if (!e->native_->EnsureTexture(e->width_, e->height_))
        continue;
      e->native_->next();
      if (vpl_encode(e, e->native_->GetCurrentTexture(), nullptr, nullptr) ==
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
int vpl_set_bitrate(void *encoder, int32_t kbs) {
  try {
    VplEncoder *p = (VplEncoder *)encoder;
    mfxStatus sts = MFX_ERR_NONE;
    p->mfxEncParams_.mfx.TargetKbps = kbs;
    sts = MFXVideoENCODE_Reset(p->session_, &p->mfxEncParams_);
    if (sts != MFX_ERR_NONE) {
      LOG_ERROR("MFXVideoENCODE_Reset failed, sts=" + std::to_string(sts));
      return -1;
    }
    return 0;
  } catch (const std::exception &e) {
    LOG_ERROR("Exception: " + e.what());
  }
  return -1;
}

int vpl_set_framerate(void *encoder, int32_t framerate) {
  LOG_WARN("not support change framerate");
  return -1;
}
}
