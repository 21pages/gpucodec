#ifndef WIN_H
#define WIN_H

#include <d3d11.h>
#include <iostream>
#include <wrl/client.h>

#include "../../common.h"

using Microsoft::WRL::ComPtr;

#define IF_FAILED_RETURN(X)                                                    \
  if (FAILED(hr = (X))) {                                                      \
    return hr;                                                                 \
  }

#define IF_FAILED_THROW(X)                                                     \
  if (FAILED(hr = (X))) {                                                      \
    throw hr;                                                                  \
  }
#define SAFE_RELEASE(p)                                                        \
  {                                                                            \
    if ((p)) {                                                                 \
      (p)->Release();                                                          \
      (p) = nullptr;                                                           \
    }                                                                          \
  }
#define LUID(desc)                                                             \
  (((int64_t)desc.AdapterLuid.HighPart << 32) | desc.AdapterLuid.LowPart)
#define HRB(f) MS_CHECK(f, return false;)
#define HRI(f) MS_CHECK(f, return -1;)
#define HRP(f) MS_CHECK(f, return nullptr;)
#define MS_CHECK(f, ...)                                                       \
  do {                                                                         \
    HRESULT __ms_hr__ = (f);                                                   \
    if (FAILED(__ms_hr__)) {                                                   \
      std::clog                                                                \
          << #f "  ERROR@" << __LINE__ << __FUNCTION__ << ": (" << std::hex    \
          << __ms_hr__ << std::dec << ") "                                     \
          << std::error_code(__ms_hr__, std::system_category()).message()      \
          << std::endl                                                         \
          << std::flush;                                                       \
      __VA_ARGS__                                                              \
    }                                                                          \
  } while (false)

class NativeDevice {
public:
  bool Init(int64_t luid, ID3D11Device *device, int pool_size = 1);
  bool EnsureTexture(int width, int height);
  bool SetTexture(ID3D11Texture2D *texture);
  HANDLE GetSharedHandle();
  ID3D11Texture2D *GetCurrentTexture();
  int next();

private:
  bool InitFromLuid(int64_t luid);
  bool InitFromDevice(ID3D11Device *device);
  bool SetMultithreadProtected();

public:
  // Direct3D 11
  ComPtr<IDXGIFactory1> factory1_ = nullptr;
  ComPtr<IDXGIAdapter> adapter_ = nullptr;
  ComPtr<IDXGIAdapter1> adapter1_ = nullptr;
  ComPtr<ID3D11Device> device_ = nullptr;
  ComPtr<ID3D11DeviceContext> context_ = nullptr;
  int count_;
  int index_ = 0;

private:
  std::vector<ComPtr<ID3D11Texture2D>> texture_;
};

class Adapter {
public:
  bool Init(IDXGIAdapter1 *adapter1);

private:
  bool SetMultithreadProtected();

public:
  ComPtr<IDXGIAdapter> adapter_ = nullptr;
  ComPtr<IDXGIAdapter1> adapter1_ = nullptr;
  ComPtr<ID3D11Device> device_ = nullptr;
  ComPtr<ID3D11DeviceContext> context_ = nullptr;
  DXGI_ADAPTER_DESC1 desc1_;
};

class Adapters {
public:
  bool Init(AdapterVendor vendor);

public:
  ComPtr<IDXGIFactory1> factory1_ = nullptr;
  std::vector<std::unique_ptr<Adapter>> adapters_;
};

bool createBgraBmpFile(ID3D11Device *device, ID3D11DeviceContext *deviceContext,
                       ID3D11Texture2D *texture, const std::string &strBmpFile);

#endif