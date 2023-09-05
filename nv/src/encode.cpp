#define FFNV_LOG_FUNC
#define FFNV_DEBUG_LOG_FUNC

#include <Samples/NvCodec/NvEncoder/NvEncoderCuda.h>
#include <Samples/NvCodec/NvEncoder/NvEncoderD3D11.h>
#include <Samples/Utils/Logger.h>
#include <Samples/Utils/NvCodecUtils.h>
#include <Samples/Utils/NvEncoderCLIOptions.h>
#include <dynlink_cuda.h>
#include <dynlink_loader.h>
#include <fstream>
#include <iostream>
#include <memory>

#include <d3d11.h>
#include <d3d9.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

#include "callback.h"
#include "common.h"
#include "system.h"

simplelogger::Logger *logger =
    simplelogger::LoggerFactory::CreateConsoleLogger();

// #define CONFIG_NV_OPTIMUS_FOR_DEV

static void load_driver(CudaFunctions **pp_cuda_dl,
                        NvencFunctions **pp_nvenc_dl) {
  if (cuda_load_functions(pp_cuda_dl, NULL) < 0) {
    NVENC_THROW_ERROR("cuda_load_functions failed", NV_ENC_ERR_GENERIC);
  }
  if (nvenc_load_functions(pp_nvenc_dl, NULL) < 0) {
    NVENC_THROW_ERROR("nvenc_load_functions failed", NV_ENC_ERR_GENERIC);
  }
}

static void free_driver(CudaFunctions **pp_cuda_dl,
                        NvencFunctions **pp_nvenc_dl) {
  if (*pp_nvenc_dl) {
    nvenc_free_functions(pp_nvenc_dl);
    *pp_nvenc_dl = NULL;
  }
  if (*pp_cuda_dl) {
    cuda_free_functions(pp_cuda_dl);
    *pp_cuda_dl = NULL;
  }
}

extern "C" int nv_encode_driver_support() {
  try {
    CudaFunctions *cuda_dl = NULL;
    NvencFunctions *nvenc_dl = NULL;
    load_driver(&cuda_dl, &nvenc_dl);
    free_driver(&cuda_dl, &nvenc_dl);
    return 0;
  } catch (const std::exception &e) {
  }
  return -1;
}

struct NvencEncoder {
  void *handle;
  std::unique_ptr<NativeDevice> nativeDevice = nullptr;
  NvEncoderD3D11 *pEnc = NULL;
  CudaFunctions *cuda_dl = NULL;
  NvencFunctions *nvenc_dl = NULL;
  int32_t width;
  int32_t height;
  NV_ENC_BUFFER_FORMAT format;
  CUcontext cuContext = NULL;

  NvencEncoder(int32_t width, int32_t height, NV_ENC_BUFFER_FORMAT format)
      : width(width), height(height), format(format) {
    load_driver(&cuda_dl, &nvenc_dl);
  }
};

extern "C" int nv_destroy_encoder(void *encoder) {
  try {
    NvencEncoder *e = (NvencEncoder *)encoder;
    if (e) {
      if (e->pEnc) {
        e->pEnc->DestroyEncoder();
        delete e->pEnc;
        e->pEnc = nullptr;
      }
      if (e->cuContext) {
        e->cuda_dl->cuCtxDestroy(e->cuContext);
      }
      free_driver(&e->cuda_dl, &e->nvenc_dl);
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  }
  return -1;
}

extern "C" void *nv_new_encoder(void *handle, int64_t luid, API api,
                                DataFormat dataFormat, int32_t width,
                                int32_t height, int32_t kbs, int32_t framerate,
                                int32_t gop) {
  NvencEncoder *e = NULL;
  try {
    if (width % 2 != 0 || height % 2 != 0) {
      goto _exit;
    }
    if (dataFormat != H264 && dataFormat != H265) {
      goto _exit;
    }
    GUID guidCodec = NV_ENC_CODEC_H264_GUID;
    if (H265 == dataFormat) {
      guidCodec = NV_ENC_CODEC_HEVC_GUID;
    }
    NV_ENC_BUFFER_FORMAT surfaceFormat =
        NV_ENC_BUFFER_FORMAT_ARGB; // NV_ENC_BUFFER_FORMAT_ABGR;

    e = new NvencEncoder(width, height, surfaceFormat);
    if (!e) {
      std::cout << "failed to new NvencEncoder" << std::endl;
      goto _exit;
    }
    if (!ck(e->cuda_dl->cuInit(0))) {
      goto _exit;
    }

    if (API_DX11 == api) {
      e->nativeDevice = std::make_unique<NativeDevice>();
#ifdef CONFIG_NV_OPTIMUS_FOR_DEV
      if (!e->nativeDevice->Init(luid, nullptr))
        goto _exit;
#else
      if (!e->nativeDevice->Init(luid, (ID3D11Device *)handle))
        goto _exit;
#endif
    } else {
      goto _exit;
    }
    int nGpu = 0, gpu = 0;
    if (!ck(e->cuda_dl->cuDeviceGetCount(&nGpu))) {
      goto _exit;
    }
    if (gpu >= nGpu) {
      std::cout << "GPU ordinal out of range. Should be within [" << 0 << ", "
                << nGpu - 1 << "]" << std::endl;
      goto _exit;
    }
    CUdevice cuDevice = 0;
    if (!ck(e->cuda_dl->cuDeviceGet(&cuDevice, gpu))) {
      goto _exit;
    }
    char szDeviceName[80];
    if (!ck(e->cuda_dl->cuDeviceGetName(szDeviceName, sizeof(szDeviceName),
                                        cuDevice))) {
      goto _exit;
    }
    if (!ck(e->cuda_dl->cuCtxCreate(&e->cuContext, 0, cuDevice))) {
      goto _exit;
    }

    int nExtraOutputDelay = 0;
    e->pEnc = new NvEncoderD3D11(
        e->cuda_dl, e->nvenc_dl, e->nativeDevice->device_.Get(), e->width,
        e->height, e->format, nExtraOutputDelay, false, false); // no delay
    NV_ENC_INITIALIZE_PARAMS initializeParams = {0};
    NV_ENC_CONFIG encodeConfig = {0};

    initializeParams.encodeConfig = &encodeConfig;
    e->pEnc->CreateDefaultEncoderParams(
        &initializeParams, NV_ENC_CODEC_H264_GUID,
        NV_ENC_PRESET_P3_GUID /*NV_ENC_PRESET_LOW_LATENCY_HP_GUID*/,
        NV_ENC_TUNING_INFO_LOW_LATENCY);

    // no delay
    initializeParams.encodeConfig->frameIntervalP = 1;
    initializeParams.encodeConfig->rcParams.lookaheadDepth = 0;

    // bitrate
    initializeParams.encodeConfig->rcParams.averageBitRate = kbs * 1000;
    // framerate
    initializeParams.frameRateNum = framerate;
    initializeParams.frameRateDen = 1;
    // gop
    if (gop == MAX_GOP) {
      gop = NVENC_INFINITE_GOPLENGTH;
    }
    initializeParams.encodeConfig->gopLength = gop;

    e->pEnc->CreateEncoder(&initializeParams);
    e->handle = handle;

    return e;
  } catch (const std::exception &ex) {
    std::cout << ex.what();
    goto _exit;
  }

_exit:
  if (e) {
    nv_destroy_encoder(e);
    delete e;
  }
  return NULL;
}

#ifdef CONFIG_NV_OPTIMUS_FOR_DEV
static int copy_texture(NvencEncoder *e, void *src, void *dst) {
  ComPtr<ID3D11Device> src_device = (ID3D11Device *)e->handle;
  ComPtr<ID3D11DeviceContext> src_deviceContext;
  src_device->GetImmediateContext(src_deviceContext.ReleaseAndGetAddressOf());
  ComPtr<ID3D11Texture2D> src_tex = (ID3D11Texture2D *)src;
  ComPtr<ID3D11Texture2D> dst_tex = (ID3D11Texture2D *)dst;
  HRESULT hr;

  D3D11_TEXTURE2D_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  src_tex->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.BindFlags = 0;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging_tex;
  src_device->CreateTexture2D(&desc, NULL,
                              staging_tex.ReleaseAndGetAddressOf());
  src_deviceContext->CopyResource(staging_tex.Get(), src_tex.Get());

  D3D11_MAPPED_SUBRESOURCE map;
  src_deviceContext->Map(staging_tex.Get(), 0, D3D11_MAP_READ, 0, &map);
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[desc.Width * desc.Height * 4]);
  memcpy(buffer.get(), map.pData, desc.Width * desc.Height * 4);
  src_deviceContext->Unmap(staging_tex.Get(), 0);

  D3D11_BOX Box;
  Box.left = 0;
  Box.right = desc.Width;
  Box.top = 0;
  Box.bottom = desc.Height;
  Box.front = 0;
  Box.back = 1;
  e->nativeDevice->context_->UpdateSubresource(dst_tex.Get(), 0, &Box,
                                               buffer.get(), desc.Width * 4,
                                               desc.Width * desc.Height * 4);

  return 0;
}
#endif

extern "C" int nv_encode(void *encoder, void *texture, EncodeCallback callback,
                         void *obj) {
  try {
    NvencEncoder *e = (NvencEncoder *)encoder;
    NvEncoderD3D11 *pEnc = e->pEnc;
    CUcontext cuContext = e->cuContext;
    bool encoded = false;
    std::vector<NvPacket> vPacket;
    const NvEncInputFrame *pEncInput = pEnc->GetNextInputFrame();

#ifdef CONFIG_NV_OPTIMUS_FOR_DEV
    copy_texture(e, texture, pEncInput->inputPtr);
#else
    ID3D11Texture2D *pBgraTextyure =
        reinterpret_cast<ID3D11Texture2D *>(pEncInput->inputPtr);
    e->nativeDevice->context_->CopyResource(
        pBgraTextyure, reinterpret_cast<ID3D11Texture2D *>(texture));
#endif

    pEnc->EncodeFrame(vPacket);
    for (NvPacket &packet : vPacket) {
      int32_t key = (packet.pictureType == NV_ENC_PIC_TYPE_IDR ||
                     packet.pictureType == NV_ENC_PIC_TYPE_I)
                        ? 1
                        : 0;
      if (packet.data.size() > 0) {
        if (callback)
          callback(packet.data.data(), packet.data.size(), key, obj);
        encoded = true;
      }
    }
    return encoded ? 0 : -1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  }
  return -1;
}

#define RECONFIGURE_HEAD                                                       \
  NvencEncoder *enc = (NvencEncoder *)e;                                       \
  NV_ENC_CONFIG sEncodeConfig = {0};                                           \
  NV_ENC_INITIALIZE_PARAMS sInitializeParams = {0};                            \
  sInitializeParams.encodeConfig = &sEncodeConfig;                             \
  enc->pEnc->GetInitializeParams(&sInitializeParams);                          \
  NV_ENC_RECONFIGURE_PARAMS params = {0};                                      \
  params.version = NV_ENC_RECONFIGURE_PARAMS_VER;                              \
  params.reInitEncodeParams = sInitializeParams;

#define RECONFIGURE_TAIL                                                       \
  if (enc->pEnc->Reconfigure(&params)) {                                       \
    return 0;                                                                  \
  }

extern "C" int nv_test_encode(void *outDescs, int32_t maxDescNum,
                              int32_t *outDescNum, API api,
                              DataFormat dataFormat, int32_t width,
                              int32_t height, int32_t kbs, int32_t framerate,
                              int32_t gop) {
  try {
    AdapterDesc *descs = (AdapterDesc *)outDescs;
    Adapters adapters;
    if (!adapters.Init(ADAPTER_VENDOR_NVIDIA))
      return -1;
    int count = 0;
    for (auto &adapter : adapters.adapters_) {
      NvencEncoder *e = (NvencEncoder *)nv_new_encoder(
          (void *)adapter.get()->device_.Get(), LUID(adapter.get()->desc1_),
          api, dataFormat, width, height, kbs, framerate, gop);
      if (!e)
        continue;
      if (!e->nativeDevice->EnsureTexture(e->width, e->height))
        continue;
      e->nativeDevice->next();
      if (nv_encode(e, e->nativeDevice->GetCurrentTexture(), nullptr,
                    nullptr) == 0) {
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

extern "C" int nv_set_bitrate(void *e, int32_t kbs) {
  try {
    RECONFIGURE_HEAD
    params.reInitEncodeParams.encodeConfig->rcParams.averageBitRate =
        kbs * 1000;
    RECONFIGURE_TAIL
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  }
  return -1;
}

extern "C" int nv_set_framerate(void *e, int32_t kbs) {
  try {
    RECONFIGURE_HEAD
    params.reInitEncodeParams.frameRateNum = kbs * 1000;
    params.reInitEncodeParams.frameRateDen = 1;
    RECONFIGURE_TAIL
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  }
  return -1;
}