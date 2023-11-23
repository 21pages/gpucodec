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

struct encoder_input {
  ComPtr<ID3D11VideoProcessorOutputView> vp_out_view;
  mfxFrameSurface1 surface;
  ComPtr<ID3D11Texture2D> texture;
};

class MFXEncoder {
public:
  std::unique_ptr<NativeDevice> native_ = nullptr;
  MFXVideoSession session_;
  MFXVideoENCODE *mfxENC_ = nullptr;
  std::vector<mfxFrameSurface1> pEncSurfaces_;
  std::vector<mfxU8> bstData_;
  mfxBitstream mfxBS_;
  MfxVideoParamsWrapper mfxEncParams;
  ComPtr<ID3D11VideoProcessorEnumerator1> vp_enum_ = nullptr;
  ComPtr<ID3D11VideoProcessor> processor_ = nullptr;
  std::vector<encoder_input> encode_inputs;

  int width_ = 0;
  int height_ = 0;

  mfxStatus InitMFX() {
    mfxStatus sts = MFX_ERR_NONE;

    sts = InitSession(session_);
    LOG_MSDK_CHECK_STATUS(sts, "InitSession");
    sts = session_.SetHandle(MFX_HANDLE_D3D11_DEVICE, native_->device_.Get());
    LOG_MSDK_CHECK_STATUS(sts, "SetHandle");
    sts = session_.SetFrameAllocator(&alloc);
    LOG_MSDK_CHECK_STATUS(sts, "SetFrameAllocator");

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
                      int32_t framerate, int32_t gop, int32_t q_min,
                      int32_t q_max) {
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
  mfxExtCodingOption3 *codingOption3 = nullptr;
  mfxExtVideoSignalInfo *videoSignalInfo = nullptr;

  MFXEncoder *p = new MFXEncoder();
  if (!p) {
    LOG_ERROR("alloc MFXEncoder failed");
    goto _exit;
  }

  p->native_ = std::make_unique<NativeDevice>();
  if (!p->native_->Init(luid, (ID3D11Device *)handle))
    goto _exit;

  sts = p->InitMFX();
  CHECK_STATUS_GOTO(sts, "InitMFX");

  memset(&p->mfxEncParams, 0, sizeof(p->mfxEncParams));

  p->mfxEncParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;
  if (!convert_codec(dataFormat, p->mfxEncParams.mfx.CodecId)) {
    LOG_ERROR("unsupported dataFormat: " + std::to_string(dataFormat));
    return NULL;
  }
  p->mfxEncParams.mfx.RateControlMethod = MFX_RATECONTROL_ICQ;
  p->mfxEncParams.mfx.ICQQuality = 1;

  p->mfxEncParams.mfx.TargetUsage = MFX_TARGETUSAGE_BEST_QUALITY;
  p->mfxEncParams.mfx.EncodedOrder = 0;
  p->mfxEncParams.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
  // p->mfxEncParams.mfx.TargetKbps = kbs;
  p->mfxEncParams.mfx.CodecLevel = MFX_LEVEL_AVC_52; // h264 tmp
  p->mfxEncParams.mfx.CodecProfile =
      MFX_PROFILE_AVC_PROGRESSIVE_HIGH; // h264 tmp

  p->mfxEncParams.mfx.FrameInfo.CropW = w;
  p->mfxEncParams.mfx.FrameInfo.CropH = h;
  // Width must be a multiple of 16
  // Height must be a multiple of 16 in case of frame picture and a multiple of
  // 32 in case of field picture
  p->mfxEncParams.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
  p->mfxEncParams.mfx.FrameInfo.Width = MSDK_ALIGN16(w); // todo
  p->mfxEncParams.mfx.FrameInfo.Height =
      (MFX_PICSTRUCT_PROGRESSIVE == p->mfxEncParams.mfx.FrameInfo.PicStruct)
          ? MSDK_ALIGN16(h)
          : MSDK_ALIGN32(h);
  // p->mfxEncParams.mfx.FrameInfo.CropX = 0;
  // p->mfxEncParams.mfx.FrameInfo.CropY = 0;
  p->mfxEncParams.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
  p->mfxEncParams.mfx.FrameInfo.FrameRateExtN = 141400000;
  p->mfxEncParams.mfx.FrameInfo.FrameRateExtD = 2356200;

  // Configuration for low latency
  p->mfxEncParams.AsyncDepth = 1; // 1 is best for low latency
  p->mfxEncParams.mfx.GopRefDist =
      1; // 1 is best for low latency, I and P frames only

  // bool q_valid =
  //     q_min >= 0 && q_min <= 51 && q_max >= 0 && q_max <= 51 && q_min <=
  //     q_max;
  // if (q_valid) {
  //   codingOption2 = p->mfxEncParams.AddExtBuffer<mfxExtCodingOption2>();
  //   set_qp(codingOption2, q_min, q_max);
  // }

  // codingOption3 = p->mfxEncParams.AddExtBuffer<mfxExtCodingOption3>();
  // if (codingOption3) {
  //   codingOption3->ScenarioInfo = MFX_SCENARIO_DISPLAY_REMOTING;
  // }

  // videoSignalInfo = p->mfxEncParams.AddExtBuffer<mfxExtVideoSignalInfo>();
  // if (videoSignalInfo) {
  //   videoSignalInfo->ColourDescriptionPresent = 1;
  //   videoSignalInfo->MatrixCoefficients = MFX_TRANSFERMATRIX_BT709;
  // }

  p->width_ = w;
  p->height_ = h;

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
  sts = p->mfxENC_->Query(&p->mfxEncParams, &p->mfxEncParams);
  MSDK_IGNORE_MFX_STS(sts, MFX_WRN_INCOMPATIBLE_VIDEO_PARAM);
  CHECK_STATUS_GOTO(sts, "Query");

  sts = p->mfxENC_->QueryIOSurf(&p->mfxEncParams, &EncRequest);
  CHECK_STATUS_GOTO(sts, "QueryIOSurf");

  // Initialize the Media SDK encoder
  sts = p->mfxENC_->Init(&p->mfxEncParams);
  MSDK_IGNORE_MFX_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
  CHECK_STATUS_GOTO(sts, "Init");

  // Retrieve video parameters selected by encoder.
  // - BufferSizeInKB parameter is required to set bit stream buffer size
  sts = p->mfxENC_->GetVideoParam(&p->mfxEncParams);
  CHECK_STATUS_GOTO(sts, "GetVideoParam");

  nEncSurfNum = EncRequest.NumFrameSuggested;

  // Prepare Media SDK bit stream buffer
  memset(&p->mfxBS_, 0, sizeof(p->mfxBS_));
  p->mfxBS_.MaxLength = p->mfxEncParams.mfx.BufferSizeInKB * 1024;
  p->bstData_.resize(p->mfxBS_.MaxLength);
  p->mfxBS_.Data = p->bstData_.data();

  HRESULT hr = S_OK;
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC vp_desc{
      D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE,
      DXGI_RATIONAL{141400000, 2356200},
      w,
      h,
      DXGI_RATIONAL{141400000, 2356200},
      w,
      h,
      D3D11_VIDEO_USAGE_OPTIMAL_QUALITY};
  {
    ID3D11VideoProcessorEnumerator *temp;
    hr = p->native_->video_device_->CreateVideoProcessorEnumerator(&vp_desc,
                                                                   &temp);
    if (FAILED(hr))
      exit(hr);
    hr = temp->QueryInterface(IID_PPV_ARGS(&p->vp_enum_));
    if (FAILED(hr))
      exit(hr);
    temp->Release();
  }
  BOOL supported;
  hr = p->vp_enum_->CheckVideoProcessorFormatConversion(
      DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
      DXGI_FORMAT_NV12, DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709, &supported);
  if (FAILED(hr))
    exit(hr);
  if (!supported)
    exit(-1);
  D3D11_VIDEO_PROCESSOR_CAPS vp_caps{};
  hr = p->vp_enum_->GetVideoProcessorCaps(&vp_caps);
  if (FAILED(hr))
    exit(hr);
  // The first one has to be 1:1
  hr = p->native_->video_device_->CreateVideoProcessor(p->vp_enum_.Get(), 0,
                                                       &p->processor_);
  if (FAILED(hr))
    exit(hr);
  D3D11_TEXTURE2D_DESC tex_desc{};
  tex_desc.Width = EncRequest.Info.Width;
  tex_desc.Height = EncRequest.Info.Height;
  tex_desc.MipLevels = 1;
  tex_desc.ArraySize = 1;
  tex_desc.Format = DXGI_FORMAT_NV12;
  tex_desc.SampleDesc = {1, 0};
  tex_desc.Usage = D3D11_USAGE_DEFAULT;
  tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  tex_desc.MiscFlags = 0;
  p->encode_inputs.resize(EncRequest.NumFrameSuggested);
  for (mfxU16 i = 0; i < EncRequest.NumFrameSuggested; ++i) {
    HRESULT hr = p->native_->device_->CreateTexture2D(
        &tex_desc, NULL, p->encode_inputs[i].texture.ReleaseAndGetAddressOf());
    if (FAILED(hr))
      exit(hr);
    p->encode_inputs[i].surface.Data.MemId = p->encode_inputs[i].texture.Get();
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC desc{};
    desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2DARRAY;
    desc.Texture2DArray.MipSlice = 0;
    desc.Texture2DArray.FirstArraySlice = 0;
    desc.Texture2DArray.ArraySize = 1;
    hr = p->native_->video_device_->CreateVideoProcessorOutputView(
        p->encode_inputs[i].texture.Get(), p->vp_enum_.Get(), &desc,
        p->encode_inputs[i].vp_out_view.ReleaseAndGetAddressOf());
    if (FAILED(hr))
      exit(hr);
    p->encode_inputs[i].surface.Info = p->mfxEncParams.mfx.FrameInfo;
  }

  return p;
_exit:
  if (p) {
    mfx_destroy_encoder(p);
    delete p;
  }
  return NULL;
}

mfxU16 get_available_surf(MFXEncoder *p) {
  mfxU16 rtn = -1;
  for (mfxU16 index = 0; index < p->encode_inputs.size(); ++index) {
    if (!p->encode_inputs[index].surface.Data.Locked) {
      rtn = index;
      break;
    }
  }
  return rtn;
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

  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{
      0, D3D11_VPIV_DIMENSION_TEXTURE2D};
  // input_desc.Texture2D.ArraySlice = 0;
  // input_desc.Texture2D.MipSlice = 0;
  ID3D11VideoProcessorInputView *in_view;
  HRESULT hr = p->native_->video_device_->CreateVideoProcessorInputView(
      tex, p->vp_enum_.Get(), &input_desc, &in_view);
  if (FAILED(hr))
    exit(hr);
  D3D11_VIDEO_PROCESSOR_STREAM stream_desc{
      TRUE, 0, 0, 0, 0, NULL, in_view, NULL,
  };
  mfxU16 index = get_available_surf(p);
  hr = p->native_->video_context_->VideoProcessorBlt(
      p->processor_.Get(), p->encode_inputs[index].vp_out_view.Get(), 0, 1,
      &stream_desc);
  if (FAILED(hr))
    exit(hr);
  in_view->Release();

  for (;;) {
    // Encode a frame asychronously (returns immediately)
    sts = p->mfxENC_->EncodeFrameAsync(NULL, &p->encode_inputs[index].surface,
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
        syncp, INFINITE); // Synchronize. Wait until encoded frame is ready
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
  MFXEncoder *p = (MFXEncoder *)encoder;
  mfxStatus sts = MFX_ERR_NONE;
  p->mfxEncParams.mfx.TargetKbps = kbs;
  sts = MFXVideoENCODE_Reset(p->session_, &p->mfxEncParams);
  CHECK_STATUS_RETURN(sts, "MFXVideoENCODE_Reset");
  return 0;
}

int mfx_set_qp(void *encoder, int32_t q_min, int32_t q_max) {
  MFXEncoder *p = (MFXEncoder *)encoder;
  mfxStatus sts = MFX_ERR_NONE;

  mfxExtCodingOption2 *codingOption2 =
      p->mfxEncParams.GetExtBuffer<mfxExtCodingOption2>();
  if (codingOption2 != nullptr) {
    if (set_qp(codingOption2, q_min, q_max) != 0) {
      return -1;
    }
    sts = MFXVideoENCODE_Reset(p->session_, &p->mfxEncParams);
    CHECK_STATUS_RETURN(sts, "MFXVideoENCODE_Reset");
    return 0;
  } else {
    LOG_ERROR("codingOption2 is null");
    return -1;
  }
}

int mfx_set_framerate(void *encoder, int32_t framerate) {
  LOG_WARN("not support change framerate");
  return -1;
}
}
