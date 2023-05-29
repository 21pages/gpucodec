#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <list>

#include <dxgi.h>
#include <d3d11.h>

#include "win.h"

bool NativeDevice::Init(int64_t luid, ID3D11Device *device)
{
	if (device) {
		if (!Init(device)) return false;
	} else {
		if (!Init(luid)) return false;
	}
	if (!SetMultithreadProtected()) return false;
	return true;
}

bool NativeDevice::Init(int64_t luid)
{
    HRESULT hr = S_OK;

	HRB(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)factory1_.ReleaseAndGetAddressOf()));

	ComPtr<IDXGIAdapter1> tmpAdapter = nullptr;
	for (int i = 0; !FAILED(factory1_->EnumAdapters1(i, tmpAdapter.ReleaseAndGetAddressOf())); i++) {
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
	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_0,
	};
	UINT numFeatureLevels = ARRAYSIZE(featureLevels);

	D3D_FEATURE_LEVEL featureLevel;
	D3D_DRIVER_TYPE d3dDriverType = adapter1_ ? D3D_DRIVER_TYPE_UNKNOWN: D3D_DRIVER_TYPE_HARDWARE;
	hr = D3D11CreateDevice(adapter1_.Get(), d3dDriverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
		D3D11_SDK_VERSION, device_.ReleaseAndGetAddressOf(), &featureLevel, context_.ReleaseAndGetAddressOf());

	if (FAILED(hr))
	{
		return false;
	}

	if (featureLevel != D3D_FEATURE_LEVEL_11_0)
	{
		std::cerr << "Direct3D Feature Level 11 unsupported." << std::endl;
		return false;
	}
	return true;
}

bool NativeDevice::Init(ID3D11Device *device)
{
	device_ = device;
	device_->GetImmediateContext(context_.ReleaseAndGetAddressOf());
	ComPtr<IDXGIDevice> dxgiDevice = nullptr;
	HRB(device_.As(&dxgiDevice));
	HRB(dxgiDevice->GetAdapter(adapter_.ReleaseAndGetAddressOf()));
	HRB(adapter_.As(&adapter1_));
	HRB(adapter1_->GetParent(IID_PPV_ARGS(&factory1_)));

	return true;
}

bool NativeDevice::SetMultithreadProtected()
{
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
	if (texture_) {
		texture_->GetDesc(&desc);
		if (desc.Width == width && desc.Height == height 
			&& desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM 
			&& desc.MiscFlags == D3D11_RESOURCE_MISC_SHARED
			&& desc.Usage == D3D11_USAGE_DEFAULT) {
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

	HRB(device_->CreateTexture2D(&desc, nullptr, texture_.ReleaseAndGetAddressOf()));

	return true;
}

bool NativeDevice::CopyTexture(ID3D11Texture2D *texture) {
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	texture->GetDesc(&desc);
	desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
	HRB(device_->CreateTexture2D(&desc, nullptr, texture_.ReleaseAndGetAddressOf()));
	context_->CopyResource(texture_.Get(), texture);
	
	return true;
}

HANDLE NativeDevice::GetSharedHandle() {
	ComPtr<IDXGIResource> resource = nullptr;
	HRP(texture_.As(&resource));
	HANDLE sharedHandle = nullptr;
	HRP(resource->GetSharedHandle(&sharedHandle));
	return sharedHandle;
}

bool Adapter::Init(IDXGIAdapter1 *adapter1)
{
	HRESULT hr = S_OK;

	adapter1_= adapter1;
	HRB(adapter1_.As(&adapter_));

	UINT createDeviceFlags = 0;
	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_0,
	};
	UINT numFeatureLevels = ARRAYSIZE(featureLevels);

	D3D_FEATURE_LEVEL featureLevel;
	D3D_DRIVER_TYPE d3dDriverType = adapter1_ ? D3D_DRIVER_TYPE_UNKNOWN: D3D_DRIVER_TYPE_HARDWARE;
	hr = D3D11CreateDevice(adapter1_.Get(), d3dDriverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
		D3D11_SDK_VERSION, device_.ReleaseAndGetAddressOf(), &featureLevel, context_.ReleaseAndGetAddressOf());

	if (FAILED(hr))
	{
		return false;
	}

	if (featureLevel != D3D_FEATURE_LEVEL_11_0)
	{
		std::cerr << "Direct3D Feature Level 11 unsupported." << std::endl;
		return false;
	}

	HRB(adapter1->GetDesc1(&desc1_));
	if (desc1_.VendorId == ADAPTER_VENDOR_INTEL) {
		if (!SetMultithreadProtected()) return false;
	}

	return true;
}

bool Adapter::SetMultithreadProtected()
{
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

bool Adapters::Init(AdapterVendor vendor)
{
	HRESULT hr = S_OK;

	HRB(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)factory1_.ReleaseAndGetAddressOf()));

	ComPtr<IDXGIAdapter1> tmpAdapter = nullptr;
	for (int i = 0; !FAILED(factory1_->EnumAdapters1(i, tmpAdapter.ReleaseAndGetAddressOf())); i++) {
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