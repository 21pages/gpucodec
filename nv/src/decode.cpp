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

#define LOG_MODULE "CUVID"
#include "log.h"

#define NUMVERTICES 6

using namespace DirectX;

namespace {

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

void load_driver(CudaFunctions **pp_cudl, CuvidFunctions **pp_cvdl) {
  if (cuda_load_functions(pp_cudl, NULL) < 0) {
    NVDEC_THROW_ERROR("cuda_load_functions failed", CUDA_ERROR_UNKNOWN);
  }
  if (cuvid_load_functions(pp_cvdl, NULL) < 0) {
    NVDEC_THROW_ERROR("cuvid_load_functions failed", CUDA_ERROR_UNKNOWN);
  }
}

void free_driver(CudaFunctions **pp_cudl, CuvidFunctions **pp_cvdl) {
  if (*pp_cvdl) {
    cuvid_free_functions(pp_cvdl);
    *pp_cvdl = NULL;
  }
  if (*pp_cudl) {
    cuda_free_functions(pp_cudl);
    *pp_cudl = NULL;
  }
}

typedef struct _VERTEX {
  DirectX::XMFLOAT3 Pos;
  DirectX::XMFLOAT2 TexCoord;
} VERTEX;

class CuvidDecoder {
public:
  CudaFunctions *cudl_ = NULL;
  CuvidFunctions *cvdl_ = NULL;
  NvDecoder *dec_ = NULL;
  CUcontext cuContext_ = NULL;
  CUgraphicsResource cuResource_[2] = {NULL, NULL}; // r8, r8g8
  ComPtr<ID3D11Texture2D> textures_[2] = {NULL, NULL};
  ComPtr<ID3D11RenderTargetView> RTV_ = NULL;
  ComPtr<ID3D11ShaderResourceView> SRV_[2] = {NULL, NULL};
  ComPtr<ID3D11VertexShader> vertexShader_ = NULL;
  ComPtr<ID3D11PixelShader> pixelShader_ = NULL;
  ComPtr<ID3D11SamplerState> samplerLinear_ = NULL;
  ComPtr<ID3D11Texture2D> bgraTexture_ = NULL;
  std::unique_ptr<RGBToNV12> nv12torgb_ = NULL;
  std::unique_ptr<NativeDevice> native_ = nullptr;
  bool outputSharedHandle_;
  bool prepare_tried_ = false;
  bool prepare_ok_ = false;

public:
  CuvidDecoder() { load_driver(&cudl_, &cvdl_); }

  bool prepare() {
    if (prepare_tried_) {
      return prepare_ok_;
    }
    prepare_tried_ = true;

    if (!set_srv())
      return false;
    if (!set_rtv())
      return false;
    if (!set_view_port())
      return false;
    if (!set_sample())
      return false;
    if (!set_shader())
      return false;
    if (!set_vertex_buffer())
      return false;
    if (!register_texture())
      return false;

    prepare_ok_ = true;
    return true;
  }

  bool copy_cuda_frame(unsigned char *dpNv12) {
    int width = dec_->GetWidth();
    int height = dec_->GetHeight();
    int chromaHeight = dec_->GetChromaHeight();

    CUVIDAutoCtxPopper ctxPoper(cudl_, cuContext_);

    for (int i = 0; i < 2; i++) {
      CUarray dstArray;
      CUVIDAutoUnmapper unmapper(cudl_, &cuResource_[i]);
      if (!ck(cudl_->cuGraphicsSubResourceGetMappedArray(&dstArray,
                                                         cuResource_[i], 0, 0)))
        return false;
      CUDA_MEMCPY2D m = {0};
      m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
      m.srcDevice = (CUdeviceptr)(dpNv12 + (width * height) * i);
      m.srcPitch = width; // pitch
      m.dstMemoryType = CU_MEMORYTYPE_ARRAY;
      m.dstArray = dstArray;
      m.WidthInBytes = width;
      m.Height = i == 0 ? height : chromaHeight;
      if (!ck(cudl_->cuMemcpy2D(&m)))
        return false;
    }
  }

  bool draw() {
    native_->context_->Draw(NUMVERTICES, 0);
    native_->context_->Flush();

    return true;
  }

private:
  bool set_srv() {
    int width = dec_->GetWidth();
    int height = dec_->GetHeight();
    int chromaHeight = dec_->GetChromaHeight();
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
    HRB(native_->device_->CreateTexture2D(
        &desc, nullptr, textures_[0].ReleaseAndGetAddressOf()));

    desc.Format = DXGI_FORMAT_R8G8_UNORM;
    desc.Width = width / 2;
    desc.Height = chromaHeight;
    HRB(native_->device_->CreateTexture2D(
        &desc, nullptr, textures_[1].ReleaseAndGetAddressOf()));

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    srvDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(textures_[0].Get(),
                                               D3D11_SRV_DIMENSION_TEXTURE2D,
                                               DXGI_FORMAT_R8_UNORM);
    HRB(native_->device_->CreateShaderResourceView(
        textures_[0].Get(), &srvDesc, SRV_[0].ReleaseAndGetAddressOf()));

    srvDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(textures_[1].Get(),
                                               D3D11_SRV_DIMENSION_TEXTURE2D,
                                               DXGI_FORMAT_R8G8_UNORM);
    HRB(native_->device_->CreateShaderResourceView(
        textures_[1].Get(), &srvDesc, SRV_[1].ReleaseAndGetAddressOf()));

    // set SRV
    std::array<ID3D11ShaderResourceView *, 2> const textureViews = {
        SRV_[0].Get(), SRV_[1].Get()};
    native_->context_->PSSetShaderResources(0, textureViews.size(),
                                            textureViews.data());
    return true;
  }

  bool set_rtv() {
    int width = dec_->GetWidth();
    int height = dec_->GetHeight();

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
    HRB(native_->device_->CreateTexture2D(
        &desc, nullptr, bgraTexture_.ReleaseAndGetAddressOf()));
    D3D11_RENDER_TARGET_VIEW_DESC rtDesc;
    rtDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtDesc.Texture2D.MipSlice = 0;
    HRB(native_->device_->CreateRenderTargetView(
        bgraTexture_.Get(), &rtDesc, RTV_.ReleaseAndGetAddressOf()));

    const float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // clear as black
    native_->context_->ClearRenderTargetView(RTV_.Get(), clearColor);
    native_->context_->OMSetRenderTargets(1, RTV_.GetAddressOf(), NULL);

    return true;
  }

  bool set_view_port() {
    int width = dec_->GetWidth();
    int height = dec_->GetHeight();

    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)(width);
    vp.Height = (FLOAT)(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    native_->context_->RSSetViewports(1, &vp);

    return true;
  }

  bool set_sample() {
    D3D11_SAMPLER_DESC sampleDesc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
    HRB(native_->device_->CreateSamplerState(
        &sampleDesc, samplerLinear_.ReleaseAndGetAddressOf()));
    native_->context_->PSSetSamplers(0, 1, samplerLinear_.GetAddressOf());
    return true;
  }

  bool set_shader() {
    // https://gist.github.com/RomiTT/9c05d36fe339b899793a3252297a5624
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
    const char *pixleShaderCode601 = R"(
Texture2D g_txFrame0 : register(t0);
Texture2D g_txFrame1 : register(t1);
SamplerState g_Sam : register(s0);

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};
float4 PS(PS_INPUT input) : SV_TARGET{
  float y = g_txFrame0.Sample(g_Sam, input.Tex).r;
  y = 1.164383561643836 * (y - 0.0625);
  float2 uv = g_txFrame1.Sample(g_Sam, input.Tex).rg - float2(0.5f, 0.5f);
  float u = uv.x;
  float v = uv.y;
  float r = saturate(y + 1.596026785714286 * v);
  float g = saturate(y - 0.812967647237771 * v - 0.391762290094914 * u);
  float b = saturate(y + 2.017232142857142 * u);
  return float4(r, g, b, 1.0f);
}
)";
    const char *pixleShaderCode709 = R"(
Texture2D g_txFrame0 : register(t0);
Texture2D g_txFrame1 : register(t1);
SamplerState g_Sam : register(s0);

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};
float4 PS(PS_INPUT input) : SV_TARGET{
  float y = g_txFrame0.Sample(g_Sam, input.Tex).r;
  y = 1.164383561643836 * (y - 0.0625);
  float2 uv = g_txFrame1.Sample(g_Sam, input.Tex).rg - float2(0.5f, 0.5f);
  float u = uv.x;
  float v = uv.y;
  float r = saturate(y + 1.792741071428571 * v);
  float g = saturate(y - 0.532909328559444 * v - 0.21324861427373 * u);
  float b = saturate(y + 2.112401785714286 * u);
  return float4(r, g, b, 1.0f);
}
)";
    ComPtr<ID3DBlob> vsBlob = NULL;
    ComPtr<ID3DBlob> psBlob = NULL;
    HRB(D3DCompile(vertexShaderCode, strlen(vertexShaderCode), NULL, NULL, NULL,
                   "VS", "vs_4_0", 0, 0, vsBlob.ReleaseAndGetAddressOf(),
                   NULL));
    HRB(D3DCompile(pixleShaderCode601, strlen(pixleShaderCode601), NULL, NULL,
                   NULL, "PS", "ps_4_0", 0, 0, psBlob.ReleaseAndGetAddressOf(),
                   NULL));
    native_->device_->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
        vertexShader_.ReleaseAndGetAddressOf());
    native_->device_->CreatePixelShader(psBlob->GetBufferPointer(),
                                        psBlob->GetBufferSize(), nullptr,
                                        pixelShader_.ReleaseAndGetAddressOf());

    // set InputLayout
    constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2> Layout = {{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    }};
    ComPtr<ID3D11InputLayout> inputLayout = NULL;
    HRB(native_->device_->CreateInputLayout(
        Layout.data(), Layout.size(), vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(), inputLayout.GetAddressOf()));
    native_->context_->IASetInputLayout(inputLayout.Get());

    native_->context_->VSSetShader(vertexShader_.Get(), NULL, 0);
    native_->context_->PSSetShader(pixelShader_.Get(), NULL, 0);

    return true;
  }

  bool set_vertex_buffer() {
    UINT Stride = sizeof(VERTEX);
    UINT Offset = 0;
    FLOAT blendFactor[4] = {0.f, 0.f, 0.f, 0.f};
    native_->context_->OMSetBlendState(nullptr, blendFactor, 0xffffffff);

    native_->context_->IASetPrimitiveTopology(
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
    HRB(native_->device_->CreateBuffer(&BufferDesc, &InitData, &VertexBuffer));
    native_->context_->IASetVertexBuffers(0, 1, VertexBuffer.GetAddressOf(),
                                          &Stride, &Offset);

    return true;
  }

  bool register_texture() {
    CUVIDAutoCtxPopper ctxPoper(cudl_, cuContext_);

    bool ret = true;
    for (int i = 0; i < 2; i++) {
      if (!ck(cudl_->cuGraphicsD3D11RegisterResource(
              &cuResource_[i], textures_[i].Get(),
              CU_GRAPHICS_REGISTER_FLAGS_NONE))) {
        ret = false;
        break;
      }
      if (!ck(cudl_->cuGraphicsResourceSetMapFlags(
              cuResource_[i], CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD))) {
        ret = false;
        break;
      }
    }

    return ret;
  }
};

bool dataFormat_to_cuCodecID(DataFormat dataFormat, cudaVideoCodec &cuda) {
  switch (dataFormat) {
  case H264:
    cuda = cudaVideoCodec_H264;
    break;
  case H265:
    cuda = cudaVideoCodec_HEVC;
    break;
  default:
    return false;
  }
  return true;
}

} // namespace

extern "C" {

int nv_decode_driver_support() {
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

int nv_destroy_decoder(void *decoder) {
  try {
    CuvidDecoder *p = (CuvidDecoder *)decoder;
    if (p) {
      if (p->dec_) {
        delete p->dec_;
      }
      if (p->cudl_ && p->cuContext_) {
        p->cudl_->cuCtxPushCurrent(p->cuContext_);
        for (int i = 0; i < 2; i++) {
          if (p->cuResource_[i])
            p->cudl_->cuGraphicsUnregisterResource(p->cuResource_[i]);
        }
        p->cudl_->cuCtxPopCurrent(NULL);
        p->cudl_->cuCtxDestroy(p->cuContext_);
      }
      free_driver(&p->cudl_, &p->cvdl_);
    }
    return 0;
  } catch (const std::exception &e) {
    LOG_ERROR("destroy failed: " + e.what());
  }
  return -1;
}

void *nv_new_decoder(void *device, int64_t luid, API api, DataFormat dataFormat,
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
    if (!ck(p->cudl_->cuInit(0))) {
      goto _exit;
    }

    CUdevice cuDevice = 0;
    p->native_ = std::make_unique<NativeDevice>();
    if (!p->native_->Init(luid, (ID3D11Device *)device, 4))
      goto _exit;
    if (!ck(p->cudl_->cuD3D11GetDevice(&cuDevice, p->native_->adapter_.Get())))
      goto _exit;
    char szDeviceName[80];
    if (!ck(p->cudl_->cuDeviceGetName(szDeviceName, sizeof(szDeviceName),
                                      cuDevice))) {
      goto _exit;
    }

    if (!ck(p->cudl_->cuCtxCreate(&p->cuContext_, 0, cuDevice))) {
      goto _exit;
    }

    cudaVideoCodec cudaCodecID;
    if (!dataFormat_to_cuCodecID(dataFormat, cudaCodecID)) {
      goto _exit;
    }
    bool bUseDeviceFrame = true;
    bool bLowLatency = true;
    bool bDeviceFramePitched = false; // width=pitch
    p->dec_ = new NvDecoder(p->cudl_, p->cvdl_, p->cuContext_, bUseDeviceFrame,
                            cudaCodecID, bLowLatency, bDeviceFramePitched,
                            &cropRect, &resizeDim);
    /* Set operating point for AV1 SVC. It has no impact for other profiles or
     * codecs PFNVIDOPPOINTCALLBACK Callback from video parser will pick
     * operating point set to NvDecoder  */
    p->dec_->SetOperatingPoint(opPoint, bDispAllLayers);
    p->nv12torgb_ = std::make_unique<RGBToNV12>(p->native_->device_.Get(),
                                                p->native_->context_.Get());
    if (FAILED(p->nv12torgb_->Init())) {
      goto _exit;
    }
    p->outputSharedHandle_ = outputSharedHandle;
    return p;
  } catch (const std::exception &ex) {
    LOG_ERROR("destroy failed: " + ex.what());
    goto _exit;
  }

_exit:
  if (p) {
    nv_destroy_decoder(p);
    delete p;
  }
  return NULL;
}

// ref: HandlePictureDisplay
int nv_decode(void *decoder, uint8_t *data, int len, DecodeCallback callback,
              void *obj) {
  try {
    CuvidDecoder *p = (CuvidDecoder *)decoder;
    NvDecoder *dec = p->dec_;

    int nFrameReturned = dec->Decode(data, len, CUVID_PKT_ENDOFPICTURE);
    if (!nFrameReturned)
      return -1;
    cudaVideoSurfaceFormat format = dec->GetOutputFormat();
    int width = dec->GetWidth();
    int height = dec->GetHeight();
    bool decoded = false;
    for (int i = 0; i < nFrameReturned; i++) {
      uint8_t *pFrame = dec->GetFrame();
      if (!p->vertexShader_) {
        if (!p->prepare()) {
          return -1;
        }
      }
      p->native_->BeginQuery();
      if (!p->copy_cuda_frame(pFrame)) {
        p->native_->EndQuery();
        return -1;
      }
      if (!p->draw()) {
        p->native_->EndQuery();
        return -1;
      }
      if (!p->native_->EnsureTexture(width, height)) {
        LOG_ERROR("EnsureTexture failed");
        p->native_->EndQuery();
        return -1;
      }
      p->native_->next();
      // HRI(p->nv12torgb->Convert(p->bgraTexture.Get(),
      //                           p->nativeDevice->GetCurrentTexture()));
      p->native_->context_->CopyResource(p->native_->GetCurrentTexture(),
                                         p->bgraTexture_.Get());

      p->native_->EndQuery();
      if (!p->native_->Query()) {
        LOG_ERROR("Query failed");
      }

      void *opaque = nullptr;
      if (p->outputSharedHandle_) {
        HANDLE sharedHandle = p->native_->GetSharedHandle();
        if (!sharedHandle) {
          LOG_ERROR("GetSharedHandle failed");
          return -1;
        }
        opaque = sharedHandle;
      } else {
        opaque = p->native_->GetCurrentTexture();
      }

      if (callback)
        callback(opaque, obj);
      decoded = true;
    }
    return decoded ? 0 : -1;
  } catch (const std::exception &e) {
    LOG_ERROR("decode failed" + e.what());
  }
  return -1;
}

int nv_test_decode(AdapterDesc *outDescs, int32_t maxDescNum,
                   int32_t *outDescNum, API api, DataFormat dataFormat,
                   bool outputSharedHandle, uint8_t *data, int32_t length) {
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
    LOG_ERROR("test failed" + e.what());
  }
  return -1;
}
} // extern "C"
