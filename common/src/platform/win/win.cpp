#include <atomic>
#include <chrono>
#include <cstdio>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <d3d11.h>
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
  if (!InitQuery())
    return false;
  HRB(device_.As(&video_device_));
  HRB(context_.As(&video_context_));
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

  UINT createDeviceFlags =
      D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  D3D_FEATURE_LEVEL featureLevels[] = {
      D3D_FEATURE_LEVEL_11_1,
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

  // if (featureLevel != D3D_FEATURE_LEVEL_11_0) {
  //   std::cerr << "Direct3D Feature Level 11 unsupported." << std::endl;
  //   return false;
  // }
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

bool NativeDevice::InitQuery() {
  D3D11_QUERY_DESC queryDesc;
  ZeroMemory(&queryDesc, sizeof(queryDesc));
  queryDesc.Query = D3D11_QUERY_EVENT;
  queryDesc.MiscFlags = 0;
  HRB(device_->CreateQuery(&queryDesc, query_.ReleaseAndGetAddressOf()));
  return true;
}

bool NativeDevice::EnsureTexture(int width, int height) {
  D3D11_TEXTURE2D_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  if (texture_[0]) {
    texture_[0]->GetDesc(&desc);
    if ((int)desc.Width == width && (int)desc.Height == height &&
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

void NativeDevice::BeginQuery() { context_->Begin(query_.Get()); }

void NativeDevice::EndQuery() { context_->End(query_.Get()); }

bool NativeDevice::Query() {
  BOOL bResult = FALSE;
  int attempts = 0;
  while (!bResult) {
    HRESULT hr = context_->GetData(query_.Get(), &bResult, sizeof(BOOL), 0);
    if (SUCCEEDED(hr)) {
      if (bResult) {
        break;
      }
    }
    attempts++;
    if (attempts > 100)
      Sleep(1);
    if (attempts > 10000)
      break;
  }
  return bResult == TRUE;
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
  HRB(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                         (void **)factory1_.ReleaseAndGetAddressOf()));

  ComPtr<IDXGIAdapter1> tmpAdapter = nullptr;
  UINT i = 0;
  while (!FAILED(
      factory1_->EnumAdapters1(i, tmpAdapter.ReleaseAndGetAddressOf()))) {
    i++;
    DXGI_ADAPTER_DESC1 desc = DXGI_ADAPTER_DESC1();
    tmpAdapter->GetDesc1(&desc);
    if (desc.VendorId == static_cast<UINT>(vendor)) {
      auto adapter = std::make_unique<Adapter>();
      if (adapter->Init(tmpAdapter.Get())) {
        adapters_.push_back(std::move(adapter));
      }
    }
  }

  return true;
}
