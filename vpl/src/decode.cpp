#include <cstring>

#include "callback.h"
#include "common.h"
#include "system.h"

#define LOG_MODULE "VPL DECODE"
#include "log.h"

namespace {

#include <api1x_core\legacy-decode\src\util.hpp>

class VplDecoder {
public:
  std::unique_ptr<NativeDevice> native_ = nullptr;
  mfxLoader loader_ = NULL;
  mfxSession session_ = NULL;
  mfxConfig cfg_;
  mfxVariant cfgVal_[1];
  int accel_fd = 0;
  void *accelHandle = NULL;
  std::vector<mfxFrameSurface1> pmfxSurfaces_;
  std::vector<ComPtr<ID3D11Texture2D>> d3dTextures_;
  mfxVideoParam mfxDecParams_;
  bool initialized_ = false;

  void *device_;
  int64_t luid_;
  API api_;
  DataFormat codecID_;
  bool outputSharedHandle_;

  bool bt709_ = false;
  bool full_range_ = false;

  VplDecoder(void *device, int64_t luid, API api, DataFormat codecID,
             bool outputSharedHandle) {
    device_ = device;
    luid_ = luid;
    api_ = api;
    codecID_ = codecID;
    outputSharedHandle_ = outputSharedHandle;
  }

  bool init() {
    mfxStatus sts = MFX_ERR_NONE;
    native_ = std::make_unique<NativeDevice>();
    if (!native_->Init(luid_, (ID3D11Device *)device_, 4)) {
      LOG_ERROR("Failed to initialize native device");
      return false;
    }
    if (!InitVpl()) {
      LOG_ERROR("Failed to initialize VPL");
      return false;
    }

    if (!initDecParam()) {
      LOG_ERROR("Failed to initialize decode parameters");
      return false;
    }

    return true;
  }

  int decode(uint8_t *data, int len, DecodeCallback callback, void *obj) {
    mfxStatus sts = MFX_ERR_NONE;
    mfxSyncPoint syncp;
    mfxFrameSurface1 *pmfxOutSurface = NULL;
    int nIndex = 0;
    bool decoded = false;
    mfxBitstream mfxBS;

    setBitStream(&mfxBS, data, len);
    if (!initialized_) {
      if (!initializeDecode(&mfxBS, false)) {
        LOG_ERROR("Failed to initialize decode");
        return -1;
      }
      initialized_ = true;
    }
    setBitStream(&mfxBS, data, len);

    int loop_counter = 0;
    do {
      if (loop_counter++ > 100) {
        std::cerr << "mfx decode loop two many times" << std::endl;
        break;
      }

      if (MFX_WRN_DEVICE_BUSY == sts)
        Sleep(1);
      if (MFX_ERR_MORE_SURFACE == sts || MFX_ERR_NONE == sts) {
        nIndex = GetFreeSurfaceIndex(
            pmfxSurfaces_.data(),
            pmfxSurfaces_.size()); // Find free frame surface
        if (nIndex >= pmfxSurfaces_.size()) {
          LOG_ERROR("GetFreeSurfaceIndex failed, nIndex=" +
                    std::to_string(nIndex));
          return -1;
        }
      }

      sts = MFXVideoDECODE_DecodeFrameAsync(
          session_, &mfxBS, &pmfxSurfaces_[nIndex], &pmfxOutSurface, &syncp);

      if (MFX_ERR_INCOMPATIBLE_VIDEO_PARAM == sts) {
        // https://github.com/FFmpeg/FFmpeg/blob/f84412d6f4e9c1f1d1a2491f9337d7e789c688ba/libavcodec/qsvdec.c#L736
        setBitStream(&mfxBS, data, len);
        LOG_INFO("Incompatible video parameters, resetting decoder");
        if (!initializeDecode(&mfxBS, true)) {
          LOG_ERROR("Failed to initialize decode");
          return -1;
        }
        continue;
      }

      // Ignore warnings if output is available,
      if (MFX_ERR_NONE < sts && syncp)
        sts = MFX_ERR_NONE;

      if (MFX_ERR_NONE == sts)
        sts = MFXVideoCORE_SyncOperation(session_, syncp, 1000);
      if (MFX_ERR_NONE == sts) {
        if (!pmfxOutSurface) {
          LOG_ERROR("pmfxOutSurface is null");
          return -1;
        }
        if (!convert(pmfxOutSurface)) {
          LOG_ERROR("Failed to convert");
          return -1;
        }
        void *output = nullptr;
        if (outputSharedHandle_) {
          HANDLE sharedHandle = native_->GetSharedHandle();
          if (!sharedHandle) {
            LOG_ERROR("Failed to GetSharedHandle");
            return -1;
          }
          output = sharedHandle;
        } else {
          output = native_->GetCurrentTexture();
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

  void destroy() {
    if (session_) {
      MFXVideoDECODE_Close(session_);
      MFXClose(session_);
    }
    FreeAcceleratorHandle(accelHandle, accel_fd);
    accelHandle = NULL;
    accel_fd = 0;
  }

private:
  bool InitVpl() {
    mfxStatus sts = MFX_ERR_NONE;

    // Initialize oneVPL session
    loader_ = MFXLoad();
    if (!loader_) {
      LOG_ERROR("MFXLoad failed");
      return false;
    }

    // Implementation used must be the type requested from command line
    cfg_ = MFXCreateConfig(loader_);
    if (!cfg_) {
      LOG_ERROR("MFXCreateConfig failed");
      return false;
    }
    cfgVal_[0].Type = MFX_VARIANT_TYPE_U32;
    cfgVal_[0].Data.U32 = MFX_IMPL_TYPE_HARDWARE;

    sts = MFXSetConfigFilterProperty(cfg_, (mfxU8 *)"mfxImplDescription.Impl",
                                     cfgVal_[0]);
    if (MFX_ERR_NONE != sts) {
      LOG_ERROR("MFXSetConfigFilterProperty failed for Impl, sts=" +
                std::to_string(sts));
      return false;
    }
    sts = MFXCreateSession(loader_, 0, &session_);
    if (MFX_ERR_NONE != sts) {
      LOG_ERROR("MFXCreateSession failed, sts=" + std::to_string(sts));
      return false;
    }

    // Print info about implementation loaded
    ShowImplementationInfo(loader_, 0);

    // Convenience function to initialize available accelerator(s)
    accelHandle = InitAcceleratorHandle(session_, &accel_fd);

    return true;
  }

  bool initDecParam() {
    memset(&mfxDecParams_, 0, sizeof(mfxDecParams_));
    if (!convert_codec(codecID_, mfxDecParams_.mfx.CodecId)) {
      LOG_ERROR("Unsupported codec");
      return false;
    }

    mfxDecParams_.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;
    // AsyncDepth: sSpecifies how many asynchronous operations an
    // application performs before the application explicitly synchronizes the
    // result. If zero, the value is not specified
    mfxDecParams_.AsyncDepth = 1; // Not important.
    // DecodedOrder: For AVC and HEVC, used to instruct the decoder
    // to return output frames in the decoded order. Must be zero for all other
    // decoders.
    mfxDecParams_.mfx.DecodedOrder = true; // Not important.

    return true;
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

  bool initializeDecode(mfxBitstream *mfxBS, bool reinit) {
    mfxStatus sts = MFX_ERR_NONE;
    mfxU16 width, height;
    mfxU8 bitsPerPixel = 12; // NV12
    mfxU32 surfaceSize;
    mfxU8 *surfaceBuffers;

    // mfxExtVideoSignalInfo got MFX_ERR_INVALID_VIDEO_PARAM
    // mfxExtVideoSignalInfo video_signal_info = {0};

    // https://spec.oneapi.io/versions/1.1-rev-1/elements/oneVPL/source/API_ref/VPL_func_vid_decode.html#mfxvideodecode-decodeheader
    sts = MFXVideoDECODE_DecodeHeader(session_, mfxBS, &mfxDecParams_);
    if (sts != MFX_ERR_NONE) {
      LOG_ERROR("MFXVideoDECODE_DecodeHeader failed, sts=" +
                std::to_string(sts));
      return false;
    }
    // Query number required surfaces for decoder
    mfxFrameAllocRequest decRequest = {};
    MFXVideoDECODE_QueryIOSurf(session_, &mfxDecParams_, &decRequest);

    // Allocate surfaces for decoder
    allocFrames(&decRequest);

    // Initialize the Media SDK decoder
    if (reinit) {
      // https://github.com/FFmpeg/FFmpeg/blob/f84412d6f4e9c1f1d1a2491f9337d7e789c688ba/libavcodec/qsvdec.c#L181
      sts = MFXVideoDECODE_Close(session_);
      if (MFX_ERR_NONE != sts) {
        LOG_ERROR("MFXVideoDECODE_Close failed, sts=" + std::to_string(sts));
        return false;
      }
    }
    // input parameters finished, now initialize decode
    sts = MFXVideoDECODE_Init(session_, &mfxDecParams_);
    if (MFX_ERR_NONE != sts) {
      LOG_ERROR("MFXVideoDECODE_Init failed, sts=" + std::to_string(sts));
      return false;
    }
    return true;
  }

  void setBitStream(mfxBitstream *mfxBS, uint8_t *data, int len) {
    memset(mfxBS, 0, sizeof(mfxBitstream));
    mfxBS->Data = data;
    mfxBS->DataLength = len;
    mfxBS->MaxLength = len;
    mfxBS->DataFlag = MFX_BITSTREAM_COMPLETE_FRAME;
  }

  bool allocFrames(mfxFrameAllocRequest *request) {
    d3dTextures_.clear();
    pmfxSurfaces_.clear();
    D3D11_TEXTURE2D_DESC desc = {0};

    desc.Width = request->Info.Width;
    desc.Height = request->Info.Height;
    desc.MipLevels = 1;
    // number of subresources is 1 in case of not single texture
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    // desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    // desc.BindFlags = D3D11_BIND_DECODER | D3D11_BIND_RENDER_TARGET |
    //                  D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = 0;
    desc.BindFlags = D3D11_BIND_DECODER;
    for (int i = 0; i < request->NumFrameSuggested; i++) {
      ComPtr<ID3D11Texture2D> texture;
      HRESULT hr = native_->device_->CreateTexture2D(&desc, NULL, &texture);
      if (FAILED(hr)) {
        LOG_ERROR("Failed to CreateTexture2D");
        return false;
      }
      d3dTextures_.push_back(texture);
    }
    pmfxSurfaces_.resize(d3dTextures_.size());
    for (int i = 0; i < d3dTextures_.size(); i++) {
      memset(&pmfxSurfaces_[i], 0, sizeof(mfxFrameSurface1));
      pmfxSurfaces_[i].Info = mfxDecParams_.mfx.FrameInfo;
      pmfxSurfaces_[i].Data.MemId = d3dTextures_[i].Get();
    }
    return true;
  }

  bool convert(mfxFrameSurface1 *pmfxOutSurface) {
    mfxStatus sts = MFX_ERR_NONE;
    ID3D11Texture2D *texture = (ID3D11Texture2D *)pmfxOutSurface->Data.MemId;
    D3D11_TEXTURE2D_DESC desc2D;
    texture->GetDesc(&desc2D);
    if (!native_->EnsureTexture(desc2D.Width, desc2D.Height)) {
      LOG_ERROR("Failed to EnsureTexture");
      return false;
    }
    native_->next(); // comment out to remove picture shaking
    native_->BeginQuery();

    // nv12 -> bgra
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc;
    ZeroMemory(&contentDesc, sizeof(contentDesc));
    contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    contentDesc.InputFrameRate.Numerator = 60;
    contentDesc.InputFrameRate.Denominator = 1;
    contentDesc.InputWidth = pmfxOutSurface->Info.CropW;
    contentDesc.InputHeight = pmfxOutSurface->Info.CropH;
    contentDesc.OutputWidth = pmfxOutSurface->Info.CropW;
    contentDesc.OutputHeight = pmfxOutSurface->Info.CropH;
    contentDesc.OutputFrameRate.Numerator = 60;
    contentDesc.OutputFrameRate.Denominator = 1;
    DXGI_COLOR_SPACE_TYPE colorSpace_out =
        DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    DXGI_COLOR_SPACE_TYPE colorSpace_in;
    if (bt709_) {
      if (full_range_) {
        colorSpace_in = DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709;
      } else {
        colorSpace_in = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
      }
    } else {
      if (full_range_) {
        colorSpace_in = DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601;
      } else {
        colorSpace_in = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
      }
    }
    if (!native_->Process(texture, native_->GetCurrentTexture(), contentDesc,
                          colorSpace_in, colorSpace_out)) {
      LOG_ERROR("Failed to process");
      native_->EndQuery();
      return false;
    }

    native_->context_->Flush();
    native_->EndQuery();
    if (!native_->Query()) {
      LOG_ERROR("Failed to query");
      return false;
    }
    return true;
  }
};

} // namespace

extern "C" {

int vpl_destroy_decoder(void *decoder) {
  VplDecoder *p = (VplDecoder *)decoder;
  if (p) {
    p->destroy();
  }
  return 0;
}

void *vpl_new_decoder(void *device, int64_t luid, API api, DataFormat codecID,
                      bool outputSharedHandle) {
  VplDecoder *p = NULL;
  try {
    p = new VplDecoder(device, luid, api, codecID, outputSharedHandle);
    if (p) {
      if (p->init()) {
        return p;
      } else {
        LOG_ERROR("init failed");
      }
    }
  } catch (const std::exception &e) {
    LOG_ERROR("new failed: " + e.what());
  }

  if (p) {
    vpl_destroy_decoder(p);
    delete p;
    p = NULL;
  }
  return NULL;
}

int vpl_decode(void *decoder, uint8_t *data, int len, DecodeCallback callback,
               void *obj) {
  try {
    VplDecoder *p = (VplDecoder *)decoder;
    return p->decode(data, len, callback, obj);
  } catch (const std::exception &e) {
    LOG_ERROR("decode failed: " + e.what());
  }
  return -1;
}

int vpl_test_decode(AdapterDesc *outDescs, int32_t maxDescNum,
                    int32_t *outDescNum, API api, DataFormat dataFormat,
                    bool outputSharedHandle, uint8_t *data, int32_t length) {
  try {
    AdapterDesc *descs = (AdapterDesc *)outDescs;
    Adapters adapters;
    if (!adapters.Init(ADAPTER_VENDOR_INTEL))
      return -1;
    int count = 0;
    for (auto &adapter : adapters.adapters_) {
      VplDecoder *p =
          (VplDecoder *)vpl_new_decoder(nullptr, LUID(adapter.get()->desc1_),
                                        api, dataFormat, outputSharedHandle);
      if (!p)
        continue;
      if (vpl_decode(p, data, length, nullptr, nullptr) == 0) {
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
