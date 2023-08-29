#define FFNV_LOG_FUNC
#define FFNV_DEBUG_LOG_FUNC

#include <Preproc.h>
#include <Samples/NvCodec/NvDecoder/NvDecoder.h>
#include <Samples/Utils/NvCodecUtils.h>
#include <algorithm>
#include <directxcolors.h>
#include <iostream>
#include <thread>

#include "callback.h"
#include "common.h"
#include "system.h"

// #define CONFIG_CPU_CONVERT

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
  CUgraphicsResource cuResource[2] = {NULL, NULL}; // nv12, r8g8
  ComPtr<ID3D11Texture2D> nv12Texture = NULL;
  ComPtr<ID3D11Texture2D> textures[2] = {NULL, NULL};
  ComPtr<ID3D11RenderTargetView> RTV = NULL;
  ComPtr<ID3D11ShaderResourceView> SRV[2] = {NULL, NULL};
  ComPtr<ID3D11VertexShader> vertexShader = NULL;
  ComPtr<ID3D11PixelShader> pixelShader = NULL;
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
      p->cudl->cuCtxPushCurrent(p->cuContext);
      for (int i = 0; i < 2; i++) {
        if (p->cuResource[i])
          p->cudl->cuGraphicsUnregisterResource(p->cuResource[i]);
      }
      p->cudl->cuCtxPopCurrent(NULL);
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

#ifdef CONFIG_CPU_CONVERT
static bool cpu_r8g8_to_nv12_uv(CuvidDecoder *p, int width, int height) {
  uint8_t *buffer = new uint8_t[width * height / 2];
  memset(buffer, 0, width * height / 2);

  D3D11_TEXTURE2D_DESC desc;
  p->r8g8texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.BindFlags = 0;
  ComPtr<ID3D11Texture2D> stagingTexture;
  HRB(p->nativeDevice->device_->CreateTexture2D(
      &desc, nullptr, stagingTexture.ReleaseAndGetAddressOf()));
  p->nativeDevice->context_->CopyResource(stagingTexture.Get(),
                                          p->r8g8texture.Get());
  D3D11_MAPPED_SUBRESOURCE resourceDesc = {0};
  HRB(p->nativeDevice->context_->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0,
                                     &resourceDesc));
  memcpy(buffer, resourceDesc.pData, width * height / 2);
  p->nativeDevice->context_->Unmap(stagingTexture.Get(), 0);

  D3D11_BOX Box;
  Box.left = 0;
  Box.right = width;
  Box.top = 0;
  Box.bottom = height / 2;
  Box.front = 0;
  Box.back = 1;

  // p->nativeDevice->context_->UpdateSubresource(
  //     p->nv12Texture.Get(), 0, &Box, buffer, width, width * height / 2);

  delete[] buffer;
}
#endif

static bool CopyDeviceFrame(CuvidDecoder *p, unsigned char *dpNv12) {
  NvDecoder *dec = p->dec;
  int width = dec->GetWidth();
  int height = dec->GetHeight();

  if (!ck(p->cudl->cuCtxPushCurrent(p->cuContext)))
    return false;

  for (int i = 0; i < 2; i++) {
    CUarray dstArray;

    ck(p->cudl->cuGraphicsMapResources(1, &p->cuResource[i], 0));
    ck(p->cudl->cuGraphicsSubResourceGetMappedArray(&dstArray, p->cuResource[i],
                                                    0, 0));
    CUDA_MEMCPY2D m = {0};
    m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    m.srcDevice = (CUdeviceptr)(dpNv12 + (width * height) * i);
    m.srcPitch = width; // pitch
    m.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    m.dstArray = dstArray;
    m.WidthInBytes = width;
    m.Height = height / (1 << i);
    ck(p->cudl->cuMemcpy2D(&m));

    // todo: release
    ck(p->cudl->cuGraphicsUnmapResources(1, &p->cuResource[i], 0));
  }

  if (!ck(p->cudl->cuCtxPopCurrent(NULL)))
    return false;

#ifdef CONFIG_CPU_CONVERT
  cpu_r8g8_to_nv12_uv(p, width, height);
#else
  // create uv shader resource view
  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
  srvDesc.Format = DXGI_FORMAT_R8_UNORM;
  srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;
  srvDesc.Texture2D.MostDetailedMip = 0;
  HRB(p->nativeDevice->device_->CreateShaderResourceView(
      p->textures[0].Get(), &srvDesc, p->SRV[0].ReleaseAndGetAddressOf()));

  srvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
  HRB(p->nativeDevice->device_->CreateShaderResourceView(
      p->textures[1].Get(), &srvDesc, p->SRV[1].ReleaseAndGetAddressOf()));

  p->nativeDevice->context_->OMSetRenderTargets(1, p->RTV.GetAddressOf(), NULL);
  p->nativeDevice->context_->ClearRenderTargetView(p->RTV.Get(),
                                                   DirectX::Colors::Aquamarine);
  p->nativeDevice->context_->VSSetShader(p->vertexShader.Get(), NULL, 0);
  p->nativeDevice->context_->PSSetShader(p->pixelShader.Get(), NULL, 0);
  ID3D11ShaderResourceView *SRVs[2];
  SRVs[0] = p->SRV[0].Get();
  SRVs[1] = p->SRV[1].Get();
  p->nativeDevice->context_->PSSetShaderResources(0, 2, SRVs);
  p->nativeDevice->context_->Draw(4, 0);
  p->nativeDevice->context_->Flush();
#endif

  return true;
}

static bool create_register_texture(CuvidDecoder *p) {

  if (p->vertexShader)
    return true;
  NvDecoder *dec = p->dec;
  int width = dec->GetWidth();
  int height = dec->GetHeight();

  // create textures
  D3D11_TEXTURE2D_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.MiscFlags = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = 0;
  HRB(p->nativeDevice->device_->CreateTexture2D(
      &desc, nullptr, p->textures[0].ReleaseAndGetAddressOf()));

  desc.Format = DXGI_FORMAT_R8G8_UNORM;
  desc.Width = width / 2;
  desc.Height = height / 2;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  HRB(p->nativeDevice->device_->CreateTexture2D(
      &desc, nullptr, p->textures[1].ReleaseAndGetAddressOf()));

  // create shader
  // auto vsFile = GetCurrentCsoDir() + L"v.cso";
  // auto psFile = GetCurrentCsoDir() + L"p.cso";
  auto vsFile = L"nv/src/v.cso";
  auto psFile = L"nv/src/p.cso";
  HRB(InitVertexShaderFromFile(p->nativeDevice->device_.Get(), vsFile,
                               p->vertexShader.GetAddressOf(), FALSE, nullptr));
  HRB(InitPixelShaderFromFile(p->nativeDevice->device_.Get(), psFile,
                              p->pixelShader.ReleaseAndGetAddressOf()));

  // create  rtv
  D3D11_RENDER_TARGET_VIEW_DESC rtDesc;
  rtDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
  rtDesc.Texture2D.MipSlice = 0;

  if (!p->nativeDevice->EnsureTexture(width, height)) {
    std::cerr << "Failed to EnsureTexture" << std::endl;
    return false;
  }
  ID3D11Texture2D *bgraTexture = p->nativeDevice->GetCurrentTexture();
  HRB(p->nativeDevice->device_->CreateRenderTargetView(
      bgraTexture, &rtDesc, p->RTV.ReleaseAndGetAddressOf()));

  // view port
  D3D11_VIEWPORT vp;
  vp.Width = (FLOAT)(width);
  vp.Height = (FLOAT)(height);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  vp.TopLeftX = 0;
  vp.TopLeftY = 0;

  p->nativeDevice->context_->RSSetViewports(1, &vp);

  if (!ck(p->cudl->cuCtxPushCurrent(p->cuContext)))
    return false;
  bool ret = true;
  for (int i = 0; i < 2; i++) {
    if (!ck(p->cudl->cuGraphicsD3D11RegisterResource(
            &p->cuResource[i], p->textures[i].Get(),
            CU_GRAPHICS_REGISTER_FLAGS_NONE))) {
      ret = false;
      break;
    }
    if (!ck(p->cudl->cuGraphicsResourceSetMapFlags(
            p->cuResource[i], CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD))) {
      ret = false;
      break;
    }
  }
  if (!ck(p->cudl->cuCtxPopCurrent(NULL)))
    return false;

  return ret;
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
      if (!p->vertexShader) {
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
      // p->nativeDevice->next();
      // HRI(p->nv12torgb->Convert(p->nv12Texture.Get(),
      //                           p->nativeDevice->GetCurrentTexture()));
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