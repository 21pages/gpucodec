#define FFNV_LOG_FUNC
#define FFNV_DEBUG_LOG_FUNC

#include <Preproc.h>
#include <Samples/NvCodec/NvDecoder/NvDecoder.h>
#include <Samples/Utils/NvCodecUtils.h>
#include <algorithm>
#include <iostream>
#include <thread>

#include "callback.h"
#include "common.h"
#include "system.h"

static void load_driver(CudaFunctions **pp_cudl, CuvidFunctions **pp_cvdl) {
  if (cuda_load_functions(pp_cudl, NULL) < 0) {
    NVDEC_THROW_ERROR("cuda_load_functions failed", CUDA_ERROR_UNKNOWN);
  }
  if (cuvid_load_functions(pp_cvdl, NULL) < 0) {
    NVDEC_THROW_ERROR("cuvid_load_functions failed", CUDA_ERROR_UNKNOWN);
  }
}

static void free_driver(CudaFunctions **pp_cudl, CuvidFunctions **pp_cvdl) {
  if (*pp_cvdl) {
    cuvid_free_functions(pp_cvdl);
    *pp_cvdl = NULL;
  }
  if (*pp_cudl) {
    cuda_free_functions(pp_cudl);
    *pp_cudl = NULL;
  }
}

extern "C" int nv_decode_driver_support() {
  try {
    CudaFunctions *cudl = NULL;
    CuvidFunctions *cvdl = NULL;
    load_driver(&cudl, &cvdl);
    free_driver(&cudl, &cvdl);
    return 0;
  } catch (const std::exception &e) {
  }
  return -1;
}

struct CuvidDecoder {
public:
  CudaFunctions *cudl = NULL;
  CuvidFunctions *cvdl = NULL;
  NvDecoder *dec = NULL;
  CUcontext cuContext = NULL;
  CUgraphicsResource cuResourceR8 = NULL;
  CUgraphicsResource cuResourceR8G8 = NULL;
  ComPtr<ID3D11Texture2D> nv12Texture = NULL;
  ComPtr<ID3D11Texture2D> r8Texture = NULL;
  ComPtr<ID3D11Texture2D> r8g8Texture = NULL;
  std::unique_ptr<RGBToNV12> nv12torgb = NULL;
  std::unique_ptr<NativeDevice> nativeDevice = nullptr;
  bool outputSharedHandle;

  CuvidDecoder() { load_driver(&cudl, &cvdl); }
};

extern "C" int nv_destroy_decoder(void *decoder) {
  try {
    CuvidDecoder *p = (CuvidDecoder *)decoder;
    if (p) {
      if (p->dec) {
        delete p->dec;
      }
      if (p->cuResourceR8) {
        p->cudl->cuCtxPushCurrent(p->cuContext);
        p->cudl->cuGraphicsUnregisterResource(p->cuResourceR8);
        p->cudl->cuCtxPopCurrent(NULL);
      }
      if (p->cuResourceR8G8) {
        p->cudl->cuCtxPushCurrent(p->cuContext);
        p->cudl->cuGraphicsUnregisterResource(p->cuResourceR8G8);
        p->cudl->cuCtxPopCurrent(NULL);
      }
      if (p->cuContext) {
        p->cudl->cuCtxDestroy(p->cuContext);
      }
      free_driver(&p->cudl, &p->cvdl);
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  }
  return -1;
}

static bool dataFormat_to_cuCodecID(DataFormat dataFormat,
                                    cudaVideoCodec &cuda) {
  switch (dataFormat) {
  case H264:
    cuda = cudaVideoCodec_H264;
    break;
  case H265:
    cuda = cudaVideoCodec_HEVC;
    break;
  case AV1:
    cuda = cudaVideoCodec_AV1;
    break;
  default:
    return false;
  }
  return true;
}

extern "C" void *nv_new_decoder(void *device, int64_t luid, API api,
                                DataFormat dataFormat,
                                bool outputSharedHandle) {
  CuvidDecoder *p = NULL;
  try {
    (void)api;
    p = new CuvidDecoder();
    if (!p) {
      goto _exit;
    }
    Rect cropRect = {};
    Dim resizeDim = {};
    unsigned int opPoint = 0;
    bool bDispAllLayers = false;
    if (!ck(p->cudl->cuInit(0))) {
      goto _exit;
    }

    CUdevice cuDevice = 0;
    p->nativeDevice = std::make_unique<NativeDevice>();
    if (!p->nativeDevice->Init(luid, (ID3D11Device *)device))
      goto _exit;
    if (!ck(p->cudl->cuD3D11GetDevice(&cuDevice,
                                      p->nativeDevice->adapter_.Get())))
      goto _exit;
    char szDeviceName[80];
    if (!ck(p->cudl->cuDeviceGetName(szDeviceName, sizeof(szDeviceName),
                                     cuDevice))) {
      goto _exit;
    }

    if (!ck(p->cudl->cuCtxCreate(&p->cuContext, 0, cuDevice))) {
      goto _exit;
    }

    cudaVideoCodec cudaCodecID;
    if (!dataFormat_to_cuCodecID(dataFormat, cudaCodecID)) {
      goto _exit;
    }
    bool bUseDeviceFrame = true;
    bool bLowLatency = true;
    p->dec =
        new NvDecoder(p->cudl, p->cvdl, p->cuContext, bUseDeviceFrame,
                      cudaCodecID, bLowLatency, false, &cropRect, &resizeDim);
    /* Set operating point for AV1 SVC. It has no impact for other profiles or
     * codecs PFNVIDOPPOINTCALLBACK Callback from video parser will pick
     * operating point set to NvDecoder  */
    p->dec->SetOperatingPoint(opPoint, bDispAllLayers);
    p->nv12torgb = std::make_unique<RGBToNV12>(p->nativeDevice->device_.Get(),
                                               p->nativeDevice->context_.Get());
    if (FAILED(p->nv12torgb->Init())) {
      goto _exit;
    }
    p->outputSharedHandle = outputSharedHandle;
    return p;
  } catch (const std::exception &ex) {
    std::cout << ex.what();
    goto _exit;
  }

_exit:
  if (p) {
    nv_destroy_decoder(p);
    delete p;
  }
  return NULL;
}

static bool CopyDeviceFrame(CuvidDecoder *p, unsigned char *dpNv12) {
  NvDecoder *dec = p->dec;
  int width = dec->GetWidth();
  int height = dec->GetHeight();

  if (!ck(p->cudl->cuCtxPushCurrent(p->cuContext)))
    return false;
  CUarray dstArray;

  ck(p->cudl->cuGraphicsMapResources(1, &p->cuResourceR8, 0));
  ck(p->cudl->cuGraphicsSubResourceGetMappedArray(&dstArray, p->cuResourceR8, 0,
                                                  0));
  CUDA_MEMCPY2D m = {0};
  m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
  m.srcDevice = (CUdeviceptr)dpNv12;
  m.srcPitch = dec->GetWidth(); // nPitch;
  m.dstMemoryType = CU_MEMORYTYPE_ARRAY;
  m.dstArray = dstArray;
  m.WidthInBytes = dec->GetWidth();
  m.Height = height;
  ck(p->cudl->cuMemcpy2D(&m));

  ck(p->cudl->cuGraphicsUnmapResources(1, &p->cuResourceR8, 0));

  CUarray dstArrayR8G8;
  ck(p->cudl->cuGraphicsMapResources(1, &p->cuResourceR8G8, 0));
  ck(p->cudl->cuGraphicsSubResourceGetMappedArray(&dstArrayR8G8,
                                                  p->cuResourceR8G8, 0, 0));
  CUDA_MEMCPY2D m2 = {0};
  m2.srcMemoryType = CU_MEMORYTYPE_DEVICE;
  m2.srcDevice = (CUdeviceptr)(dpNv12 + width * height);
  // m2.srcY = height;
  m2.srcPitch = dec->GetWidth();
  m2.dstMemoryType = CU_MEMORYTYPE_ARRAY;
  m2.dstArray = dstArrayR8G8;
  // m2.dstHost = dstArrayR8G8;
  // m2.dstPitch = width / 2;
  // m2.dstXInBytes = 0;
  // m2.dstY = 0;
  m2.WidthInBytes = dec->GetWidth();
  m2.Height = height / 2;
  ck(p->cudl->cuMemcpy2D(&m2));

  ck(p->cudl->cuGraphicsUnmapResources(1, &p->cuResourceR8G8, 0));

  if (!ck(p->cudl->cuCtxPopCurrent(NULL)))
    return false;

  // p->nativeDevice->context_->CopyResource(p->nv12Texture.Get(),
  //                                         p->r8Texture.Get());

  // p->nativeDevice->context_->CopySubresourceRegion(
  //     p->nv12Texture.Get(), 0, 0, 0, 0, p->r8Texture.Get(), 0, nullptr);
  // p->nativeDevice->context_->CopySubresourceRegion(
  //     p->nv12Texture.Get(), 1, 0, 0, 0, p->r8g8Texture.Get(), 0, nullptr);
  // p->nv12torgb->Convert(p->r8Texture.Get(), p->nv12Texture.Get());
  // p->nv12torgb->Convert(p->r8g8Texture.Get(), p->nv12Texture.Get());

  // p->nativeDevice->context_->UpdateSubresource()

  //
  uint8_t *buffer = new uint8_t[width * height * 3 / 2];
  memset(buffer, 0, width * height * 3 / 2);

  D3D11_TEXTURE2D_DESC desc;
  p->r8Texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.BindFlags = 0;
  static ComPtr<ID3D11Texture2D> r8StagingTexture;
  if (!r8StagingTexture) {
    HRB(p->nativeDevice->device_->CreateTexture2D(
        &desc, nullptr, r8StagingTexture.ReleaseAndGetAddressOf()));
  }
  p->nativeDevice->context_->CopyResource(r8StagingTexture.Get(),
                                          p->r8Texture.Get());
  D3D11_MAPPED_SUBRESOURCE r8ResourceDesc = {0};
  HRB(p->nativeDevice->context_->Map(r8StagingTexture.Get(), 0, D3D11_MAP_READ,
                                     0, &r8ResourceDesc));
  memcpy(buffer, r8ResourceDesc.pData, width * height);
  p->nativeDevice->context_->Unmap(r8StagingTexture.Get(), 0);

  p->r8g8Texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.BindFlags = 0;
  static ComPtr<ID3D11Texture2D> r8g8StagingTexture;
  if (!r8g8StagingTexture) {
    HRB(p->nativeDevice->device_->CreateTexture2D(
        &desc, nullptr, r8g8StagingTexture.ReleaseAndGetAddressOf()));
  }
  p->nativeDevice->context_->CopyResource(r8g8StagingTexture.Get(),
                                          p->r8g8Texture.Get());
  D3D11_MAPPED_SUBRESOURCE r8g8ResourceDesc = {0};
  HRB(p->nativeDevice->context_->Map(r8g8StagingTexture.Get(), 0,
                                     D3D11_MAP_READ, 0, &r8g8ResourceDesc));
  memcpy(buffer + width * height, r8g8ResourceDesc.pData, width * height / 2);
  // for (int i = 0; i < width * height / 2; i++) {
  //   uint8_t *p = buffer + width * height;
  //   if (p[i] != 0) {
  //     printf("%d\n", p[i]);
  //   }
  // }
  p->nativeDevice->context_->Unmap(r8g8StagingTexture.Get(), 0);

  D3D11_BOX Box;
  Box.left = 0;
  Box.right = width;
  Box.top = 0;
  Box.bottom = height;
  Box.front = 0;
  Box.back = 1;

  p->nativeDevice->context_->UpdateSubresource(
      p->nv12Texture.Get(), 0, &Box, buffer, width, width * height * 3 / 2);

  delete[] buffer;

  return true;
}

static bool create_register_texture(CuvidDecoder *p) {
  if (p->nv12Texture)
    return true;
  D3D11_TEXTURE2D_DESC desc;
  NvDecoder *dec = p->dec;
  int width = dec->GetWidth();
  int height = dec->GetHeight();

  ZeroMemory(&desc, sizeof(desc));
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_NV12;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.MiscFlags = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  desc.CPUAccessFlags = 0;
  HRB(p->nativeDevice->device_->CreateTexture2D(
      &desc, nullptr, p->nv12Texture.ReleaseAndGetAddressOf()));

  desc.Format = DXGI_FORMAT_R8_UNORM;
  HRB(p->nativeDevice->device_->CreateTexture2D(
      &desc, nullptr, p->r8Texture.ReleaseAndGetAddressOf()));

  desc.Format = DXGI_FORMAT_R8G8_UNORM;
  desc.Width = width / 2;
  desc.Height = height / 2;
  HRB(p->nativeDevice->device_->CreateTexture2D(
      &desc, nullptr, p->r8g8Texture.ReleaseAndGetAddressOf()));

  if (!ck(p->cudl->cuCtxPushCurrent(p->cuContext)))
    return false;
  if (!ck(p->cudl->cuGraphicsD3D11RegisterResource(
          &p->cuResourceR8, p->r8Texture.Get(),
          CU_GRAPHICS_REGISTER_FLAGS_NONE)))
    return false;
  if (!ck(p->cudl->cuGraphicsResourceSetMapFlags(
          p->cuResourceR8, CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD)))
    return false;
  if (!ck(p->cudl->cuGraphicsD3D11RegisterResource(
          &p->cuResourceR8G8, p->r8g8Texture.Get(),
          CU_GRAPHICS_REGISTER_FLAGS_NONE)))
    return false;
  if (!ck(p->cudl->cuGraphicsResourceSetMapFlags(
          p->cuResourceR8G8, CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD)))
    return false;
  if (!ck(p->cudl->cuCtxPopCurrent(NULL)))
    return false;

  return true;
}

extern "C" int nv_decode(void *decoder, uint8_t *data, int len,
                         DecodeCallback callback, void *obj) {
  try {
    CuvidDecoder *p = (CuvidDecoder *)decoder;
    NvDecoder *dec = p->dec;

    int nFrameReturned = dec->Decode(data, len, CUVID_PKT_ENDOFPICTURE);
    if (!nFrameReturned)
      return -1;
    cudaVideoSurfaceFormat format = dec->GetOutputFormat();
    int width = dec->GetWidth();
    int height = dec->GetHeight();
    bool decoded = false;
    for (int i = 0; i < nFrameReturned; i++) {
      uint8_t *pFrame = dec->GetFrame();
      if (!p->nv12Texture) {
        if (!create_register_texture(p)) { // TODO: failed on available
          return -1;
        }
      }
      if (!CopyDeviceFrame(p, pFrame))
        return -1;
      if (!p->nativeDevice->EnsureTexture(width, height)) {
        std::cerr << "Failed to EnsureTexture" << std::endl;
        return -1;
      }
      p->nativeDevice->next();
      HRI(p->nv12torgb->Convert(p->nv12Texture.Get(),
                                p->nativeDevice->GetCurrentTexture()));
      void *opaque = nullptr;
      if (p->outputSharedHandle) {
        HANDLE sharedHandle = p->nativeDevice->GetSharedHandle();
        if (!sharedHandle) {
          std::cerr << "Failed to GetSharedHandle" << std::endl;
          return -1;
        }
        opaque = sharedHandle;
      } else {
        opaque = p->nativeDevice->GetCurrentTexture();
      }

      callback(opaque, obj);
      decoded = true;
    }
    return decoded ? 0 : -1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  }
  return -1;
}

extern "C" int nv_test_decode(AdapterDesc *outDescs, int32_t maxDescNum,
                              int32_t *outDescNum, API api,
                              DataFormat dataFormat, bool outputSharedHandle,
                              uint8_t *data, int32_t length) {
  try {
    AdapterDesc *descs = (AdapterDesc *)outDescs;
    Adapters adapters;
    if (!adapters.Init(ADAPTER_VENDOR_NVIDIA))
      return -1;
    int count = 0;
    for (auto &adapter : adapters.adapters_) {
      CuvidDecoder *p =
          (CuvidDecoder *)nv_new_decoder(nullptr, LUID(adapter.get()->desc1_),
                                         api, dataFormat, outputSharedHandle);
      if (!p)
        continue;
      if (nv_decode(p, data, length, nullptr, nullptr) == 0) {
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