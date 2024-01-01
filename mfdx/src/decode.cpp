#include <codecapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "callback.h"
#include "common.h"
#include "system.h"

#define LOG_MODULE "MFDX"
#include "log.h"

// https://learn.microsoft.com/en-us/windows/win32/medfound/supporting-direct3d-11-video-decoding-in-media-foundation

namespace {
class MfdxDecoder {

public:
  std::unique_ptr<NativeDevice> native_ = nullptr;
  ComPtr<IMFActivate> activate_ = nullptr;
  ComPtr<ID3D11VideoDecoder> video_decoder_ = nullptr;
  ComPtr<IMFDXGIDeviceManager> dxgi_device_manager_ = nullptr;
  ComPtr<IMFTransform> transform_ = nullptr;
  ComPtr<IMFAttributes> attributes_ = nullptr;
  ComPtr<IMFMediaType> input_media_type_ = nullptr;
  ComPtr<IMFMediaType> output_media_type_ = nullptr;
  ComPtr<IMFSample> sample_ = nullptr;
  ComPtr<IMFMediaBuffer> buffer_ = nullptr;
  ComPtr<IMFMediaBuffer> output_buffer_ = nullptr;

  void *device_;
  int64_t luid_;
  API api_;
  DataFormat codecID_;
  bool outputSharedHandle_;

  bool bt709_ = false;
  bool full_range_ = false;

  int width_ = 1920;
  int height_ = 1080;

public:
  MfdxDecoder(void *device, int64_t luid, API api, DataFormat codecID,
              bool outputSharedHandle) {
    device_ = device;
    luid_ = luid;
    api_ = api;
    codecID_ = codecID;
    outputSharedHandle_ = outputSharedHandle;
  }

  bool init() {
    native_ = std::make_unique<NativeDevice>();
    if (!native_->Init(luid_, (ID3D11Device *)device_, 4)) {
      LOG_ERROR("Failed to initialize native device");
      return false;
    }
    HRESULT hr = initMf();
    if (FAILED(hr)) {
      return false;
    }

    return true;
  };

  void destroy() {
    MFShutdown();
    CoUninitialize();
  }

private:
  HRESULT initMf() {
    HRESULT hr = S_OK;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
      return hr;
    }

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
      return hr;
    }

    IMFActivate **acts = nullptr;
    UINT32 actsNum = 0;
    const MFT_REGISTER_TYPE_INFO pInputType = {
        MFMediaType_Video,
        DataFormat::H264 == codecID_ ? MFVideoFormat_H264 : MFVideoFormat_HEVC,
    };
    const MFT_REGISTER_TYPE_INFO *pOutputType = nullptr;

    hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_SYNCMFT,
                   &pInputType, pOutputType, &acts, &actsNum);
    if (FAILED(hr)) {
      return hr;
    }
    if (actsNum == 0) {
      return E_FAIL;
    }
    for (int n = 0; n < actsNum; n++) {
      hr = acts[n]->ActivateObject(
          IID_IMFTransform, (void **)transform_.ReleaseAndGetAddressOf());
      if (SUCCEEDED(hr)) {
        activate_ = acts[0];
        break;
      }
    }
    for (int n = 0; n < actsNum; n++) {
      acts[n]->Release();
    }
    CoTaskMemFree(acts);
    if (!transform_) {
      return E_FAIL;
    }

    transform_->GetAttributes(attributes_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
      return hr;
    }
    UINT32 aware = 0;
    hr = attributes_->GetUINT32(MF_SA_D3D11_AWARE, &aware);
    if (FAILED(hr)) {
      return hr;
    }
    if (aware == 0) {
      return E_FAIL;
    }

    UINT token = 0;
    hr = MFCreateDXGIDeviceManager(
        &token, dxgi_device_manager_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
      return hr;
    }

    hr = dxgi_device_manager_->ResetDevice(native_->device_.Get(), token);
    if (FAILED(hr)) {
      return hr;
    }

    hr = transform_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                                    (ULONG_PTR)dxgi_device_manager_.Get());
    if (FAILED(hr)) {
      return hr;
    }

    // hr = MFCreateAttributes(attributes_.ReleaseAndGetAddressOf(), 2);
    // if (FAILED(hr)) {
    //   return hr;
    // }

    // hr = attributes_->SetUINT32(MF_SA_D3D11_AWARE, TRUE);
    // if (FAILED(hr)) {
    //   return hr;
    // }
    // hr = attributes_->SetUINT32(CODECAPI_AVLowLatencyMode, TRUE);
    // if (FAILED(hr)) {
    //   return hr;
    // }

    return S_OK;
  }

  HRESULT initDecoder() {
    HRESULT hr = S_OK;

    D3D11_VIDEO_DECODER_DESC desc = {};
    desc.Guid = codecID_ == DataFormat::H264
                    ? D3D11_DECODER_PROFILE_H264_VLD_NOFGT
                    : D3D11_DECODER_PROFILE_HEVC_VLD_MAIN;
    desc.OutputFormat = DXGI_FORMAT_NV12;
    desc.SampleWidth = width_;
    desc.SampleHeight = height_;

    UINT config_count = 0;
    hr = native_->video_device_->GetVideoDecoderConfigCount(&desc,
                                                            &config_count);
    if (FAILED(hr)) {
      return hr;
    }
    if (config_count == 0) {
      return E_FAIL;
    }
    D3D11_VIDEO_DECODER_CONFIG config = {};
    bool found = false;
    for (UINT i = 0; i < config_count; i++) {
      hr = native_->video_device_->GetVideoDecoderConfig(&desc, i, &config);
      if (FAILED(hr)) {
        return hr;
      }
      if (config.ConfigBitstreamRaw == 2 && codecID_ == DataFormat::H264) {
        found = true;
        break;
      } else if (config.ConfigBitstreamRaw == 1 &&
                 codecID_ == DataFormat::H265) {
        found = true;
        break;
      }
    }
    if (!found) {
      return E_FAIL;
    }

    native_->video_device_->CreateVideoDecoder(
        &desc, &config, video_decoder_.ReleaseAndGetAddressOf());
  }
};
} // namespace

extern "C" {

int mfdx_destroy_decoder(void *decoder) { return -1; }
void *mfdx_new_decoder(void *device, int64_t luid, int32_t api, int32_t codecID,
                       bool outputSharedHandle) {
  MfdxDecoder *p = nullptr;
  try {
    p = new MfdxDecoder(device, luid, (API)api, (DataFormat)codecID,
                        outputSharedHandle);
    if (!p) {
      goto _exit;
    }
    if (p->init())
      return p;
  } catch (const std::exception &ex) {
    LOG_ERROR("destroy failed: " + ex.what());
    goto _exit;
  }
_exit:
  if (p) {
    mfdx_destroy_decoder(p);
    delete p;
  }
  return NULL;
}

int mfdx_decode(void *decoder, uint8_t *data, int len, DecodeCallback callback,
                void *obj) {
  return -1;
}

int mfdx_test_decode(void *outDescs, int32_t maxDescNum, int32_t *outDescNum,
                     int32_t api, int32_t dataFormat, bool outputSharedHandle,
                     uint8_t *data, int32_t length) {
  return -1;
}
}