#include <atomic>
#include <chrono>
#include <cstdio>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include "win.h"

bool NativeDevice::Init(int64_t luid, ID3D11Device *device, int pool_size) {
  if (device) {
    if (!InitFromDevice(device))
      return false;
  } else {
    if (!InitFromLuid(luid))
      return false;
  }
  if (!SetMultithreadProtected())
    return false;
  count_ = pool_size;
  texture_.resize(count_);
  std::fill(texture_.begin(), texture_.end(), nullptr);
  return true;
}

bool NativeDevice::InitFromLuid(int64_t luid) {
  HRESULT hr = S_OK;

  HRB(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                         (void **)factory1_.ReleaseAndGetAddressOf()));

  ComPtr<IDXGIAdapter1> tmpAdapter = nullptr;
  UINT i = 0;
  while (!FAILED(
      factory1_->EnumAdapters1(i, tmpAdapter.ReleaseAndGetAddressOf()))) {
    i++;
    DXGI_ADAPTER_DESC1 desc = DXGI_ADAPTER_DESC1();
    tmpAdapter->GetDesc1(&desc);
    if (LUID(desc) == luid) {
      adapter1_.Swap(tmpAdapter);
      break;
    }
  }
  if (!adapter1_) {
    return false;
  }
  HRB(adapter1_.As(&adapter_));

  UINT createDeviceFlags = 0;
  D3D_FEATURE_LEVEL featureLevels[] = {
      D3D_FEATURE_LEVEL_11_0,
  };
  UINT numFeatureLevels = ARRAYSIZE(featureLevels);

  D3D_FEATURE_LEVEL featureLevel;
  D3D_DRIVER_TYPE d3dDriverType =
      adapter1_ ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
  hr = D3D11CreateDevice(adapter1_.Get(), d3dDriverType, nullptr,
                         createDeviceFlags, featureLevels, numFeatureLevels,
                         D3D11_SDK_VERSION, device_.ReleaseAndGetAddressOf(),
                         &featureLevel, context_.ReleaseAndGetAddressOf());

  if (FAILED(hr)) {
    return false;
  }

  if (featureLevel != D3D_FEATURE_LEVEL_11_0) {
    std::cerr << "Direct3D Feature Level 11 unsupported." << std::endl;
    return false;
  }
  return true;
}

bool NativeDevice::InitFromDevice(ID3D11Device *device) {
  device_ = device;
  device_->GetImmediateContext(context_.ReleaseAndGetAddressOf());
  ComPtr<IDXGIDevice> dxgiDevice = nullptr;
  HRB(device_.As(&dxgiDevice));
  HRB(dxgiDevice->GetAdapter(adapter_.ReleaseAndGetAddressOf()));
  HRB(adapter_.As(&adapter1_));
  HRB(adapter1_->GetParent(IID_PPV_ARGS(&factory1_)));

  return true;
}

bool NativeDevice::SetMultithreadProtected() {
  ComPtr<ID3D10Multithread> hmt = nullptr;
  HRB(context_.As(&hmt));
  if (!hmt->SetMultithreadProtected(TRUE)) {
    if (!hmt->GetMultithreadProtected()) {
      std::cerr << "Failed to SetMultithreadProtected" << std::endl;
      return false;
    }
  }
  return true;
}

bool NativeDevice::EnsureTexture(int width, int height) {
  D3D11_TEXTURE2D_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  if (texture_[0]) {
    texture_[0]->GetDesc(&desc);
    if (desc.Width == width && desc.Height == height &&
        desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM &&
        desc.MiscFlags == D3D11_RESOURCE_MISC_SHARED &&
        desc.Usage == D3D11_USAGE_DEFAULT) {
      return true;
    }
  }
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  desc.CPUAccessFlags = 0;

  for (int i = 0; i < texture_.size(); i++) {
    HRB(device_->CreateTexture2D(&desc, nullptr,
                                 texture_[i].ReleaseAndGetAddressOf()));
  }

  return true;
}

bool NativeDevice::SetTexture(ID3D11Texture2D *texture) {
  texture_[index_].Reset();
  texture_[index_] = texture;
  return true;
}

HANDLE NativeDevice::GetSharedHandle() {
  ComPtr<IDXGIResource> resource = nullptr;
  HRP(texture_[index_].As(&resource));
  HANDLE sharedHandle = nullptr;
  HRP(resource->GetSharedHandle(&sharedHandle));
  return sharedHandle;
}

ID3D11Texture2D *NativeDevice::GetCurrentTexture() {
  return texture_[index_].Get();
}

int NativeDevice::next() {
  index_++;
  index_ = index_ % count_;
  return index_;
}

bool Adapter::Init(IDXGIAdapter1 *adapter1) {
  HRESULT hr = S_OK;

  adapter1_ = adapter1;
  HRB(adapter1_.As(&adapter_));

  UINT createDeviceFlags = 0;
  D3D_FEATURE_LEVEL featureLevels[] = {
      D3D_FEATURE_LEVEL_11_0,
  };
  UINT numFeatureLevels = ARRAYSIZE(featureLevels);

  D3D_FEATURE_LEVEL featureLevel;
  D3D_DRIVER_TYPE d3dDriverType =
      adapter1_ ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
  hr = D3D11CreateDevice(adapter1_.Get(), d3dDriverType, nullptr,
                         createDeviceFlags, featureLevels, numFeatureLevels,
                         D3D11_SDK_VERSION, device_.ReleaseAndGetAddressOf(),
                         &featureLevel, context_.ReleaseAndGetAddressOf());

  if (FAILED(hr)) {
    return false;
  }

  if (featureLevel != D3D_FEATURE_LEVEL_11_0) {
    std::cerr << "Direct3D Feature Level 11 unsupported." << std::endl;
    return false;
  }

  HRB(adapter1->GetDesc1(&desc1_));
  if (desc1_.VendorId == ADAPTER_VENDOR_INTEL) {
    if (!SetMultithreadProtected())
      return false;
  }

  return true;
}

bool Adapter::SetMultithreadProtected() {
  ComPtr<ID3D10Multithread> hmt = nullptr;
  HRB(context_.As(&hmt));
  if (!hmt->SetMultithreadProtected(TRUE)) {
    if (!hmt->GetMultithreadProtected()) {
      std::cerr << "Failed to SetMultithreadProtected" << std::endl;
      return false;
    }
  }
  return true;
}

bool Adapters::Init(AdapterVendor vendor) {
  HRESULT hr = S_OK;

  HRB(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                         (void **)factory1_.ReleaseAndGetAddressOf()));

  ComPtr<IDXGIAdapter1> tmpAdapter = nullptr;
  UINT i = 0;
  while (!FAILED(
      factory1_->EnumAdapters1(i, tmpAdapter.ReleaseAndGetAddressOf()))) {
    i++;
    DXGI_ADAPTER_DESC1 desc = DXGI_ADAPTER_DESC1();
    tmpAdapter->GetDesc1(&desc);
    if (desc.VendorId == static_cast<int>(vendor)) {
      auto adapter = std::make_unique<Adapter>();
      if (adapter->Init(tmpAdapter.Get())) {
        adapters_.push_back(std::move(adapter));
      }
    }
  }

  return true;
}

std::wstring GetCurrentCsoDir() {
  static std::wstring result;

  do {
    if (!result.empty())
      break;

    HMODULE hModule = NULL;
    wchar_t DLLPATH[MAX_PATH + 1] = {0};
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                      (LPCTSTR)GetCurrentCsoDir, &hModule);

    //----
    ::GetModuleFileNameW(hModule, DLLPATH, MAX_PATH);
    int nLen = (int)wcslen(DLLPATH);
    for (int i = nLen - 1; i >= 0; --i) {
      if (DLLPATH[i] == '\\') {
        DLLPATH[i + 1] = 0;
        break;
      }
    }
    result = DLLPATH;
    // result += L"cso\\";
  } while (false);
  return result;
}

static HRESULT CreateShaderFromFile(const WCHAR *csoFileNameInOut,
                                    const WCHAR *hlslFileName,
                                    LPCSTR entryPoint, LPCSTR shaderModel,
                                    ID3DBlob **ppBlobOut) {
  HRESULT hr = S_OK;

  if (csoFileNameInOut &&
      D3DReadFileToBlob(csoFileNameInOut, ppBlobOut) == S_OK) {
    return hr;
  } else {
    DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    dwShaderFlags |= D3DCOMPILE_DEBUG;

    dwShaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob *errorBlob = nullptr;
    hr = D3DCompileFromFile(
        hlslFileName, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint,
        shaderModel, dwShaderFlags, 0, ppBlobOut, &errorBlob);
    if (FAILED(hr)) {
      if (errorBlob != nullptr) {
        OutputDebugStringA(
            reinterpret_cast<const char *>(errorBlob->GetBufferPointer()));
      }
      SAFE_RELEASE(errorBlob);
      return hr;
    }
    if (csoFileNameInOut) {
      return D3DWriteBlobToFile(*ppBlobOut, csoFileNameInOut, FALSE);
    }
  }

  return hr;
}

static HRESULT CompileShaderFromFile(const WCHAR *wszShaderFile,
                                     LPCSTR szEntryPoint, LPCSTR szShaderModel,
                                     ID3DBlob **ppBlobOut) {
  HRESULT hr = S_OK;
  ID3DBlob *pErrorBlob = NULL;
  DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
  dwShaderFlags |= D3DCOMPILE_DEBUG;

  dwShaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

  // hr =
  //     D3DCompileFromFile(wszShaderFile, NULL, NULL, szEntryPoint,
  //     szShaderModel,
  //         dwShaderFlags, 0, ppBlobOut, &pErrorBlob);

  ComPtr<ID3DBlob> blob;
  hr =
      CreateShaderFromFile(wszShaderFile, nullptr, "main", "vs_4_0", ppBlobOut);

  DWORD dwError = GetLastError();
  if (FAILED(hr) && pErrorBlob) {
    OutputDebugStringA(
        reinterpret_cast<const char *>(pErrorBlob->GetBufferPointer()));
  }

  SAFE_RELEASE(pErrorBlob);

  return hr;
}

HRESULT InitVertexShaderFromFile(ID3D11Device *device,
                                 const WCHAR *wszShaderFile,
                                 ID3D11VertexShader **ppID3D11VertexShader,
                                 const BOOL bCreateLayout,
                                 ID3D11InputLayout **inputLayout) {
  HRESULT hr = S_OK;
  ID3DBlob *pVSBlob = NULL;

  try {
    IF_FAILED_THROW(
        CompileShaderFromFile(wszShaderFile, "VS", "vs_5_0", &pVSBlob));
    IF_FAILED_THROW(device->CreateVertexShader(pVSBlob->GetBufferPointer(),
                                               pVSBlob->GetBufferSize(), NULL,
                                               ppID3D11VertexShader));

    if (bCreateLayout) {
      D3D11_INPUT_ELEMENT_DESC layout[] = {
          {"SV_Position", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
           D3D11_INPUT_PER_VERTEX_DATA, 0},
      };
      UINT numElements = ARRAYSIZE(layout);

      IF_FAILED_THROW(device->CreateInputLayout(
          layout, numElements, pVSBlob->GetBufferPointer(),
          pVSBlob->GetBufferSize(), inputLayout));
    }
  } catch (HRESULT) {
  }

  SAFE_RELEASE(pVSBlob);

  return hr;
}

HRESULT InitPixelShaderFromFile(ID3D11Device *device,
                                const WCHAR *wszShaderFile,
                                ID3D11PixelShader **ppID3D11PixelShader) {
  HRESULT hr = S_OK;
  ID3DBlob *pPSBlob = NULL;

  try {
    IF_FAILED_THROW(
        CompileShaderFromFile(wszShaderFile, "PS", "ps_5_0", &pPSBlob));
    IF_FAILED_THROW(device->CreatePixelShader(pPSBlob->GetBufferPointer(),
                                              pPSBlob->GetBufferSize(), NULL,
                                              ppID3D11PixelShader));
  } catch (HRESULT) {
  }

  SAFE_RELEASE(pPSBlob);

  return hr;
}