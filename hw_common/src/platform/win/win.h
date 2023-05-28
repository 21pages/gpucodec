#ifndef WIN_H
#define WIN_H

#include <iostream>
#include <d3d11.h>
#include <wrl/client.h>

#include "../../common.h"

using Microsoft::WRL::ComPtr;

#define SAFE_RELEASE(p) { if ((p)) { (p)->Release(); (p) = nullptr; } }
#define HRB(hr) { if (FAILED((hr))) { std::cerr << "HR = " << (hr); return false; } }
#define LUID(desc) (((int64_t)desc.AdapterLuid.HighPart << 32) | desc.AdapterLuid.LowPart)

class Texture_Lifetime_Keeper {
public:
    Texture_Lifetime_Keeper(void *texture) {
        d3d11_texture = (ID3D11Texture2D*)texture;
    }
private:
    ComPtr<ID3D11Texture2D> d3d11_texture;
};

class NativeDevice {
public:
    bool Init(int64_t luid, ID3D11Device *device);
    bool CreateTexture(int width, int height);
private:
    bool Init(int64_t luid);
    bool Init(ID3D11Device *device);
    bool SetMultithreadProtected();
public:
    // Direct3D 11
    ComPtr<IDXGIFactory1> factory1_ = nullptr;
    ComPtr<IDXGIAdapter> adapter_ = nullptr;
    ComPtr<IDXGIAdapter1> adapter1_ = nullptr;
    ComPtr<ID3D11Device> device_ = nullptr;
    ComPtr<ID3D11DeviceContext> context_ = nullptr;
    ComPtr<ID3D11Texture2D> texture_ = nullptr;
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

#endif