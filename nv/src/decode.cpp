#define FFNV_LOG_FUNC
#define FFNV_DEBUG_LOG_FUNC

#include <DirectXMath.h>
#include <Preproc.h>
#include <Samples/NvCodec/NvDecoder/NvDecoder.h>
#include <Samples/Utils/NvCodecUtils.h>
#include <algorithm>
#include <array>
#include <d3dcompiler.h>
#include <directxcolors.h>
#include <iostream>
#include <thread>

#include "callback.h"
#include "common.h"
#include "system.h"

#define NUMVERTICES 6

using namespace DirectX;

class CUVIDAutoUnmapper {
  CudaFunctions *cudl_ = NULL;
  CUgraphicsResource *pCuResource_ = NULL;

public:
  CUVIDAutoUnmapper(CudaFunctions *cudl, CUgraphicsResource *pCuResource)
      : cudl_(cudl), pCuResource_(pCuResource) {
    if (!ck(cudl->cuGraphicsMapResources(1, pCuResource, 0))) {
      NVDEC_THROW_ERROR("cuGraphicsMapResources failed", CUDA_ERROR_UNKNOWN);
    }
  }
  ~CUVIDAutoUnmapper() {
    if (!ck(cudl_->cuGraphicsUnmapResources(1, pCuResource_, 0))) {
      NVDEC_THROW_ERROR("cuGraphicsUnmapResources failed", CUDA_ERROR_UNKNOWN);
    }
  }
};

class CUVIDAutoCtxPopper {
  CudaFunctions *cudl_ = NULL;

public:
  CUVIDAutoCtxPopper(CudaFunctions *cudl, CUcontext cuContext) : cudl_(cudl) {
    if (!ck(cudl->cuCtxPushCurrent(cuContext))) {
      NVDEC_THROW_ERROR("cuCtxPopCurrent failed", CUDA_ERROR_UNKNOWN);
    }
  }
  ~CUVIDAutoCtxPopper() {
    if (!ck(cudl_->cuCtxPopCurrent(NULL))) {
      NVDEC_THROW_ERROR("cuCtxPopCurrent failed", CUDA_ERROR_UNKNOWN);
    }
  }
};

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
  CUgraphicsResource cuResource[2] = {NULL, NULL}; // r8, r8g8
  ComPtr<ID3D11Texture2D> nv12Texture = NULL;
  ComPtr<ID3D11Texture2D> textures[2] = {NULL, NULL};
  ComPtr<ID3D11RenderTargetView> RTV = NULL;
  ComPtr<ID3D11ShaderResourceView> SRV[2] = {NULL, NULL};
  ComPtr<ID3D11VertexShader> vertexShader = NULL;
  ComPtr<ID3D11PixelShader> pixelShader = NULL;
  ComPtr<ID3D11SamplerState> samplerLinear = NULL;
  ComPtr<ID3D11Texture2D> bgraTexture = NULL;
  std::unique_ptr<RGBToNV12> nv12torgb = NULL;
  std::unique_ptr<NativeDevice> nativeDevice = nullptr;
  bool outputSharedHandle;
  bool prepare_tried = false;
  bool prepare_ok = false;

  CuvidDecoder() { load_driver(&cudl, &cvdl); }
};

typedef struct _VERTEX {
  DirectX::XMFLOAT3 Pos;
  DirectX::XMFLOAT2 TexCoord;
} VERTEX;

extern "C" int nv_destroy_decoder(void *decoder) {
  try {
    CuvidDecoder *p = (CuvidDecoder *)decoder;
    if (p) {
      if (p->dec) {
        delete p->dec;
      }
      if (p->cudl && p->cuContext) {
        p->cudl->cuCtxPushCurrent(p->cuContext);
        for (int i = 0; i < 2; i++) {
          if (p->cuResource[i])
            p->cudl->cuGraphicsUnregisterResource(p->cuResource[i]);
        }
        p->cudl->cuCtxPopCurrent(NULL);
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
    bool bDeviceFramePitched = false; // width=pitch
    p->dec = new NvDecoder(p->cudl, p->cvdl, p->cuContext, bUseDeviceFrame,
                           cudaCodecID, bLowLatency, bDeviceFramePitched,
                           &cropRect, &resizeDim);
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

static bool copy_cuda_frame(CuvidDecoder *p, unsigned char *dpNv12) {
  NvDecoder *dec = p->dec;
  int width = dec->GetWidth();
  int height = dec->GetHeight();
  int chromaHeight = dec->GetChromaHeight();

  CUVIDAutoCtxPopper ctxPoper(p->cudl, p->cuContext);

  for (int i = 0; i < 2; i++) {
    CUarray dstArray;
    CUVIDAutoUnmapper unmapper(p->cudl, &p->cuResource[i]);
    if (!ck(p->cudl->cuGraphicsSubResourceGetMappedArray(
            &dstArray, p->cuResource[i], 0, 0)))
      return false;
    CUDA_MEMCPY2D m = {0};
    m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    m.srcDevice = (CUdeviceptr)(dpNv12 + (width * height) * i);
    m.srcPitch = width; // pitch
    m.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    m.dstArray = dstArray;
    m.WidthInBytes = width;
    m.Height = i == 0 ? height : chromaHeight;
    if (!ck(p->cudl->cuMemcpy2D(&m)))
      return false;
  }
}

static bool draw(CuvidDecoder *p) {
  // set SRV
  std::array<ID3D11ShaderResourceView *, 2> const textureViews = {
      p->SRV[0].Get(), p->SRV[1].Get()};
  p->nativeDevice->context_->PSSetShaderResources(0, textureViews.size(),
                                                  textureViews.data());

  UINT Stride = sizeof(VERTEX);
  UINT Offset = 0;
  FLOAT blendFactor[4] = {0.f, 0.f, 0.f, 0.f};
  p->nativeDevice->context_->OMSetBlendState(nullptr, blendFactor, 0xffffffff);

  const float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // clear as black
  p->nativeDevice->context_->ClearRenderTargetView(p->RTV.Get(), clearColor);
  p->nativeDevice->context_->OMSetRenderTargets(1, p->RTV.GetAddressOf(), NULL);
  p->nativeDevice->context_->VSSetShader(p->vertexShader.Get(), NULL, 0);
  p->nativeDevice->context_->PSSetShader(p->pixelShader.Get(), NULL, 0);
  p->nativeDevice->context_->PSSetSamplers(0, 1,
                                           p->samplerLinear.GetAddressOf());
  p->nativeDevice->context_->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  // set VertexBuffers
  VERTEX Vertices[NUMVERTICES] = {
      {XMFLOAT3(-1.0f, -1.0f, 0), XMFLOAT2(0.0f, 1.0f)},
      {XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f)},
      {XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f)},
      {XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f)},
      {XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f)},
      {XMFLOAT3(1.0f, 1.0f, 0), XMFLOAT2(1.0f, 0.0f)},
  };
  D3D11_BUFFER_DESC BufferDesc;
  RtlZeroMemory(&BufferDesc, sizeof(BufferDesc));
  BufferDesc.Usage = D3D11_USAGE_DEFAULT;
  BufferDesc.ByteWidth = sizeof(VERTEX) * NUMVERTICES;
  BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  BufferDesc.CPUAccessFlags = 0;
  D3D11_SUBRESOURCE_DATA InitData;
  RtlZeroMemory(&InitData, sizeof(InitData));
  InitData.pSysMem = Vertices;
  ComPtr<ID3D11Buffer> VertexBuffer = nullptr;
  // Create vertex buffer
  HRB(p->nativeDevice->device_->CreateBuffer(&BufferDesc, &InitData,
                                             &VertexBuffer));
  p->nativeDevice->context_->IASetVertexBuffers(
      0, 1, VertexBuffer.GetAddressOf(), &Stride, &Offset);

  // draw
  p->nativeDevice->context_->Draw(NUMVERTICES, 0);
  p->nativeDevice->context_->Flush();

  return true;
}

static bool create_srv(CuvidDecoder *p) {
  NvDecoder *dec = p->dec;
  int width = dec->GetWidth();
  int height = dec->GetHeight();
  int chromaHeight = dec->GetChromaHeight();
  printf("width: %d, height %d, chromaHeight:%d\n", width, height,
         chromaHeight);

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
  desc.Height = chromaHeight;
  HRB(p->nativeDevice->device_->CreateTexture2D(
      &desc, nullptr, p->textures[1].ReleaseAndGetAddressOf()));

  D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
  srvDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(p->textures[0].Get(),
                                             D3D11_SRV_DIMENSION_TEXTURE2D,
                                             DXGI_FORMAT_R8_UNORM);
  HRB(p->nativeDevice->device_->CreateShaderResourceView(
      p->textures[0].Get(), &srvDesc, p->SRV[0].ReleaseAndGetAddressOf()));

  srvDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(p->textures[1].Get(),
                                             D3D11_SRV_DIMENSION_TEXTURE2D,
                                             DXGI_FORMAT_R8G8_UNORM);
  HRB(p->nativeDevice->device_->CreateShaderResourceView(
      p->textures[1].Get(), &srvDesc, p->SRV[1].ReleaseAndGetAddressOf()));

  return true;
}

static bool create_rtv(CuvidDecoder *p) {
  NvDecoder *dec = p->dec;
  int width = dec->GetWidth();
  int height = dec->GetHeight();

  D3D11_TEXTURE2D_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.MiscFlags = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  desc.CPUAccessFlags = 0;
  HRB(p->nativeDevice->device_->CreateTexture2D(
      &desc, nullptr, p->bgraTexture.ReleaseAndGetAddressOf()));
  D3D11_RENDER_TARGET_VIEW_DESC rtDesc;
  rtDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
  rtDesc.Texture2D.MipSlice = 0;
  HRB(p->nativeDevice->device_->CreateRenderTargetView(
      p->bgraTexture.Get(), &rtDesc, p->RTV.ReleaseAndGetAddressOf()));

  return true;
}

static bool set_view_port(CuvidDecoder *p) {
  NvDecoder *dec = p->dec;
  int width = dec->GetWidth();
  int height = dec->GetHeight();

  D3D11_VIEWPORT vp;
  vp.Width = (FLOAT)(width);
  vp.Height = (FLOAT)(height);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  vp.TopLeftX = 0;
  vp.TopLeftY = 0;
  p->nativeDevice->context_->RSSetViewports(1, &vp);

  return true;
}

static bool create_sample(CuvidDecoder *p) {
  D3D11_SAMPLER_DESC sampleDesc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
  HRB(p->nativeDevice->device_->CreateSamplerState(
      &sampleDesc, p->samplerLinear.ReleaseAndGetAddressOf()));

  return true;
}

static bool create_shader(CuvidDecoder *p) {
  const char *vertexShaderCode = R"(
struct VS_INPUT
{
    float4 Pos : POSITION;
    float2 Tex : TEXCOORD;
};

struct VS_OUTPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};
VS_OUTPUT VS(VS_INPUT input)
{
    return input;
}
)";
  const char *pixleShaderCode = R"(
Texture2D g_txFrame0 : register(t0);
Texture2D g_txFrame1 : register(t1);
SamplerState g_Sam : register(s0);

struct VertexImageOut
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

float4 PS(VertexImageOut input) : SV_TARGET{
  float y = g_txFrame0.Sample(g_Sam, input.Tex).r;
  float2 uv = g_txFrame1.Sample(g_Sam, input.Tex).rg - float2(0.5f, 0.5f);
  float u = uv.x;
  float v = uv.y;
  float r = y + 1.14f * v;
  float g = y - 0.394f * u - 0.581f * v;
  float b = y + 2.03f * u;
  return float4(r, g, b, 1.0f);
}
)";
  ComPtr<ID3DBlob> vsBlob = NULL;
  ComPtr<ID3DBlob> psBlob = NULL;
  HRB(D3DCompile(vertexShaderCode, strlen(vertexShaderCode), NULL, NULL, NULL,
                 "VS", "vs_4_0", 0, 0, vsBlob.ReleaseAndGetAddressOf(), NULL));
  HRB(D3DCompile(pixleShaderCode, strlen(pixleShaderCode), NULL, NULL, NULL,
                 "PS", "ps_4_0", 0, 0, psBlob.ReleaseAndGetAddressOf(), NULL));
  p->nativeDevice->device_->CreateVertexShader(
      vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
      p->vertexShader.ReleaseAndGetAddressOf());
  p->nativeDevice->device_->CreatePixelShader(
      psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
      p->pixelShader.ReleaseAndGetAddressOf());

  // set InputLayout
  constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2> Layout = {{
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  }};
  ComPtr<ID3D11InputLayout> inputLayout = NULL;
  HRB(p->nativeDevice->device_->CreateInputLayout(
      Layout.data(), Layout.size(), vsBlob->GetBufferPointer(),
      vsBlob->GetBufferSize(), inputLayout.GetAddressOf()));
  p->nativeDevice->context_->IASetInputLayout(inputLayout.Get());

  return true;
}

static bool register_texture(CuvidDecoder *p) {
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

static bool prepare(CuvidDecoder *p) {
  if (p->prepare_tried) {
    return p->prepare_ok;
  }
  p->prepare_tried = true;

  if (!create_srv(p))
    return false;
  if (!create_rtv(p))
    return false;
  if (!set_view_port(p))
    return false;
  if (!create_sample(p))
    return false;
  if (!create_shader(p))
    return false;
  if (!register_texture(p))
    return false;

  p->prepare_ok = true;
  return true;
}

// ref: HandlePictureDisplay
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
        if (!prepare(p)) {
          return -1;
        }
      }
      if (!copy_cuda_frame(p, pFrame))
        return -1;
      if (!draw(p))
        return -1;
      if (!p->nativeDevice->EnsureTexture(width, height)) {
        std::cerr << "Failed to EnsureTexture" << std::endl;
        return -1;
      }
      p->nativeDevice->next();
      HRI(p->nv12torgb->Convert(p->bgraTexture.Get(),
                                p->nativeDevice->GetCurrentTexture()));
      // p->nativeDevice->context_->CopyResource(
      //     p->nativeDevice->GetCurrentTexture(), p->bgraTexture.Get());

      // static int saved = 0;
      // saved += 1;
      // if (saved == 8)
      //   createBgraBmpFile(p->nativeDevice->device_.Get(),
      //                     p->nativeDevice->context_.Get(),
      //                     p->bgraTexture.Get(), L"nv.bmp");

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

      if (callback)
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