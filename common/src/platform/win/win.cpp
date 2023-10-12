#include <atomic>
#include <chrono>
#include <cstdio>
#include <list>
#include <mutex>
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

static HRESULT CreateBmpFile(LPCWSTR wszBmpFile, BYTE *pData,
                             const UINT uiFrameSize, const UINT uiWidth,
                             const UINT uiHeight) {
  HRESULT hr = S_OK;

  HANDLE hFile = INVALID_HANDLE_VALUE;
  DWORD dwWritten;
  UINT uiStride;

  BYTE header24[54] = {0x42, 0x4d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                       0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  DWORD dwSizeFile = uiWidth * uiHeight * 3;
  dwSizeFile += 54;
  header24[2] = dwSizeFile & 0x000000ff;
  header24[3] = static_cast<BYTE>((dwSizeFile & 0x0000ff00) >> 8);
  header24[4] = static_cast<BYTE>((dwSizeFile & 0x00ff0000) >> 16);
  header24[5] = (dwSizeFile & 0xff000000) >> 24;
  dwSizeFile -= 54;
  header24[18] = uiWidth & 0x000000ff;
  header24[19] = (uiWidth & 0x0000ff00) >> 8;
  header24[20] = static_cast<BYTE>((uiWidth & 0x00ff0000) >> 16);
  header24[21] = (uiWidth & 0xff000000) >> 24;

  header24[22] = uiHeight & 0x000000ff;
  header24[23] = (uiHeight & 0x0000ff00) >> 8;
  header24[24] = static_cast<BYTE>((uiHeight & 0x00ff0000) >> 16);
  header24[25] = (uiHeight & 0xff000000) >> 24;

  header24[34] = dwSizeFile & 0x000000ff;
  header24[35] = (dwSizeFile & 0x0000ff00) >> 8;
  header24[36] = static_cast<BYTE>((dwSizeFile & 0x00ff0000) >> 16);
  header24[37] = static_cast<BYTE>((dwSizeFile & 0xff000000) >> 24);

  try {
    hFile = CreateFileW(wszBmpFile, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

    IF_FAILED_THROW(hFile == INVALID_HANDLE_VALUE ? E_FAIL : S_OK);

    IF_FAILED_THROW(WriteFile(hFile, (LPCVOID)header24, 54, &dwWritten, 0) ==
                    FALSE);
    IF_FAILED_THROW(dwWritten == 0 ? E_FAIL : S_OK);

    uiStride = uiWidth * 3;
    BYTE *Tmpbufsrc = pData + (uiFrameSize - uiStride);

    for (UINT i = 0; i < uiHeight; i++) {

      IF_FAILED_THROW(WriteFile(hFile, (LPCVOID)Tmpbufsrc, uiStride, &dwWritten,
                                0) == FALSE);
      IF_FAILED_THROW(dwWritten == 0 ? E_FAIL : S_OK);

      Tmpbufsrc -= uiStride;
    }
  } catch (HRESULT) {
  }

  if (hFile != INVALID_HANDLE_VALUE)
    CloseHandle(hFile);

  return hr;
}

static LPCWSTR ConvertToLPCWSTR(const std::string &str) {
  std::wstring wstr(str.begin(), str.end());

  int bufferSize = static_cast<int>((wstr.size() + 1) * sizeof(wchar_t));
  LPWSTR buffer = new wchar_t[bufferSize];
  wcscpy_s(buffer, bufferSize, wstr.c_str());

  return buffer;
};

bool createBgraBmpFile(ID3D11Device *device, ID3D11DeviceContext *deviceContext,
                       ID3D11Texture2D *texture,
                       const std::string &strBmpFile) {
  D3D11_TEXTURE2D_DESC desc = {};
  HRESULT hr;
  texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.BindFlags = 0;
  ComPtr<ID3D11Texture2D> bgraStagingTexture;
  hr = device->CreateTexture2D(&desc, nullptr,
                               bgraStagingTexture.GetAddressOf());
  IF_FAILED_THROW(hr);
  deviceContext->CopyResource(bgraStagingTexture.Get(), texture);

  D3D11_MAPPED_SUBRESOURCE ResourceDesc = {};
  deviceContext->Map(bgraStagingTexture.Get(), 0, D3D11_MAP_READ, 0,
                     &ResourceDesc);

  UINT uiImageSize = desc.Width * desc.Height * 3;
  BYTE *pDataRgb = new (std::nothrow) BYTE[uiImageSize];
  BYTE *pDataRgbaColor = (BYTE *)ResourceDesc.pData;
  BYTE *pDataRgbColor = pDataRgb;
  for (UINT i = 0; i < desc.Height; i++) {
    for (UINT j = 0; j < desc.Width; j++) {
      if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        // bgr             bgra
        *pDataRgbColor++ = *pDataRgbaColor++;
        *pDataRgbColor++ = *pDataRgbaColor++;
        *pDataRgbColor++ = *pDataRgbaColor++;
        pDataRgbaColor++;
      } else {
        // bgr             rgba
        pDataRgbColor[0] = pDataRgbaColor[2];
        pDataRgbColor[1] = pDataRgbaColor[1];
        pDataRgbColor[2] = pDataRgbaColor[0];
        pDataRgbColor += 3;
        pDataRgbaColor += 4;
      }
    }
  }
  LPCWSTR wszBmpFile = ConvertToLPCWSTR(strBmpFile);
  hr =
      CreateBmpFile(wszBmpFile, pDataRgb, uiImageSize, desc.Width, desc.Height);
  delete[] pDataRgb;
  IF_FAILED_THROW(hr);
  deviceContext->Unmap(bgraStagingTexture.Get(), 0);
}