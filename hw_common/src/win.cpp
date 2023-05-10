#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <list>

#include <dxgi.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <d3d11_1.h>

#include "win.h"

struct AdatperOutputs {
	IDXGIAdapter1* adapter;
	DXGI_ADAPTER_DESC1 desc;
	std::vector<IDXGIOutput*> outputs;
	AdatperOutputs():adapter(nullptr) {};
	AdatperOutputs(AdatperOutputs&& src) noexcept
	{
		adapter = src.adapter;
		src.adapter = nullptr;
		desc = src.desc;
		outputs = std::move(src.outputs);
	}
	AdatperOutputs(const AdatperOutputs& src)
	{
		adapter = src.adapter;
		adapter->AddRef();
		desc = src.desc;
		outputs = src.outputs;
		for (size_t i = 0; i < outputs.size(); ++i) {
			outputs[i]->AddRef();
		}
	}
	~AdatperOutputs()
	{
		for (size_t i = 0; i < outputs.size(); ++i) {
			outputs[i]->Release();
		}
		if (adapter)
			adapter->Release();
	}
};

void get_first_adapter_output(IDXGIFactory2* factory2, IDXGIAdapter1** adapter_out, IDXGIOutput1** output_out)
{
	UINT num_adapters = 0;
	AdatperOutputs curent_adapter;
	std::vector<AdatperOutputs> Adapters;
	IDXGIAdapter1* selected_adapter = nullptr;
	IDXGIOutput1* selected_output = nullptr;
	HRESULT hr = S_OK;
	while (factory2->EnumAdapters1(num_adapters, &curent_adapter.adapter) != DXGI_ERROR_NOT_FOUND) {
		IDXGIOutput* output;
		UINT num_outout = 0;
		while (curent_adapter.adapter->EnumOutputs(num_outout, &output) != DXGI_ERROR_NOT_FOUND) {
			if (!selected_output) {
				IDXGIOutput1* temp;
				hr = output->QueryInterface(IID_PPV_ARGS(&temp));
				if (SUCCEEDED(hr)) {
					selected_output = temp;
					selected_adapter = curent_adapter.adapter;
					selected_adapter->AddRef();
			//		break;
				}
			}
			curent_adapter.outputs.push_back(output);
			DXGI_OUTPUT_DESC desc;
			output->GetDesc(&desc);
			++num_outout;
		}
		//if(selected_output) break;
		curent_adapter.adapter->GetDesc1(&curent_adapter.desc);
		Adapters.push_back(std::move(curent_adapter));
		++num_adapters;
	}
	*adapter_out = selected_adapter;
	*output_out = selected_output;
}


class dx_device_context {
public:
	dx_device_context()
	{
		// This is what matters (the 1)
		// Only a guid of fatory2 will not work
		HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory2));
		if (FAILED(hr)) exit(hr);
		get_first_adapter_output(factory2, &adapter1, &output1);
		D3D_FEATURE_LEVEL levels[]{
			D3D_FEATURE_LEVEL_11_1
		};
		hr = D3D11CreateDevice(adapter1, D3D_DRIVER_TYPE_UNKNOWN, NULL,
			D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			levels, 1, D3D11_SDK_VERSION, &device, NULL, &context);
		if (FAILED(hr)) exit(hr);
		hr = device->QueryInterface(IID_PPV_ARGS(&video_device));
		if (FAILED(hr)) exit(hr);
		hr = context->QueryInterface(IID_PPV_ARGS(&video_context));
		if (FAILED(hr)) exit(hr);
		hr = context->QueryInterface(IID_PPV_ARGS(&hmt));
		if (FAILED(hr)) exit(hr);
		//This is required for MFXVideoCORE_SetHandle
		hr = hmt->SetMultithreadProtected(TRUE);
		if (FAILED(hr)) exit(hr);
	}
	~dx_device_context()
	{
		if(hmt) hmt->Release();
		if (video_context) video_context->Release();
		if (video_device) video_device->Release();
		if (context) context->Release();
		if (device) device->Release();
		if (output1) output1->Release();
		if (adapter1) adapter1->Release();
		if (factory2) factory2->Release();
	}
	IDXGIFactory2* factory2;
	IDXGIAdapter1* adapter1;
	IDXGIOutput1* output1;
	ID3D11Device* device;
	ID3D11DeviceContext* context;
	ID3D11VideoDevice* video_device;
	ID3D11VideoContext* video_context;
	ID3D10Multithread* hmt;
	HMODULE debug_mod = NULL;
};



bool NativeDevice::Init(AdapterVendor vendor)
{
    HRESULT hr = S_OK;

	HRB(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)m_DXGIFactory1.ReleaseAndGetAddressOf()));
	for (int i = 0, ComPtr<IDXGIAdapter1> tmpAdapter; !FAILED(m_DXGIFactory1->EnumAdapters1(i, tmpAdapter.ReleaseAndGetAddressOf())); i++) {
		DXGI_ADAPTER_DESC1 desc = DXGI_ADAPTER_DESC1();
		tmpAdapter->GetDesc1(&desc);
		if (vendor == AdapterVendor::ANY) {
			m_DXGIAdapter1.Swap(tmpAdapter);
			break;
		} else {
			if (desc.VendorId == static_cast<int>(vendor)) {
				m_DXGIAdapter1.Swap(tmpAdapter);
				break;
			}
		}
	}

    // 创建D3D设备 和 D3D设备上下文
    UINT createDeviceFlags = 0;
// #if defined(DEBUG) || defined(_DEBUG)  
//     createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
// #endif
    // 特性等级数组
    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    UINT numFeatureLevels = ARRAYSIZE(featureLevels);

    D3D_FEATURE_LEVEL featureLevel;
    D3D_DRIVER_TYPE d3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
	hr = D3D11CreateDevice(m_DXGIAdapter1.Get(), d3dDriverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
		D3D11_SDK_VERSION, m_pd3dDevice.ReleaseAndGetAddressOf(), &featureLevel, m_pd3dImmediateContext.ReleaseAndGetAddressOf());

	if (hr == E_INVALIDARG)
	{
		// Direct3D 11.0 的API不承认D3D_FEATURE_LEVEL_11_1，所以我们需要尝试特性等级11.0以及以下的版本
		hr = D3D11CreateDevice(nullptr, d3dDriverType, nullptr, createDeviceFlags, &featureLevels[1], numFeatureLevels - 1,
			D3D11_SDK_VERSION, m_pd3dDevice.ReleaseAndGetAddressOf(), &featureLevel, m_pd3dImmediateContext.ReleaseAndGetAddressOf());
	}

    if (FAILED(hr))
    {
        return false;
    }

    // 检测是否支持特性等级11.0或11.1
    if (featureLevel != D3D_FEATURE_LEVEL_11_0 && featureLevel != D3D_FEATURE_LEVEL_11_1)
    {
        MessageBox(0, L"Direct3D Feature Level 11 unsupported.", 0, 0);
        return false;
    }

    // 检测 MSAA支持的质量等级
    m_pd3dDevice->CheckMultisampleQualityLevels(
        DXGI_FORMAT_R8G8B8A8_UNORM, 4, &m_4xMsaaQuality);
    if(m_4xMsaaQuality <= 0) 
	{
		return false;
	}

    ComPtr<IDXGIDevice> dxgiDevice = nullptr;
    ComPtr<IDXGIAdapter> dxgiAdapter = nullptr;
    ComPtr<IDXGIFactory1> dxgiFactory1 = nullptr;   // D3D11.0(包含DXGI1.1)的接口类
    ComPtr<IDXGIFactory2> dxgiFactory2 = nullptr;   // D3D11.1(包含DXGI1.2)特有的接口类

    // 为了正确创建 DXGI交换链，首先我们需要获取创建 D3D设备 的 DXGI工厂，否则会引发报错：
    // "IDXGIFactory::CreateSwapChain: This function is being called with a device from a different IDXGIFactory."
    HRB(m_pd3dDevice.As(&dxgiDevice));
    HRB(dxgiDevice->GetAdapter(dxgiAdapter.ReleaseAndGetAddressOf()));
    HRB(dxgiAdapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(dxgiFactory1.ReleaseAndGetAddressOf())));

    // 查看该对象是否包含IDXGIFactory2接口
    hr = dxgiFactory1.As(&dxgiFactory2);
    // 如果包含，则说明支持D3D11.1
    if (dxgiFactory2 != nullptr)
    {
        HRB(m_pd3dDevice.As(&m_pd3dDevice1));
        HRB(m_pd3dImmediateContext.As(&m_pd3dImmediateContext1));
        // 填充各种结构体用以描述交换链
        DXGI_SWAP_CHAIN_DESC1 sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.Width = m_ClientWidth;
        sd.Height = m_ClientHeight;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        // 是否开启4倍多重采样？
        if (m_Enable4xMsaa)
        {
            sd.SampleDesc.Count = 4;
            sd.SampleDesc.Quality = m_4xMsaaQuality - 1;
        }
        else
        {
            sd.SampleDesc.Count = 1;
            sd.SampleDesc.Quality = 0;
        }
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 1;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        sd.Flags = 0;

        DXGI_SWAP_CHAIN_FULLSCREEN_DESC fd;
        fd.RefreshRate.Numerator = 60;
        fd.RefreshRate.Denominator = 1;
        fd.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
        fd.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        fd.Windowed = TRUE;
        // 为当前窗口创建交换链
        HRB(dxgiFactory2->CreateSwapChainForHwnd(m_pd3dDevice.Get(), m_hMainWnd, &sd, &fd, nullptr, m_pSwapChain1.ReleaseAndGetAddressOf()));
        HRB(m_pSwapChain1.As(&m_pSwapChain));
    }
    else
    {
        // 填充DXGI_SWAP_CHAIN_DESC用以描述交换链
        DXGI_SWAP_CHAIN_DESC sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferDesc.Width = m_ClientWidth;
        sd.BufferDesc.Height = m_ClientHeight;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
        // 是否开启4倍多重采样？
        if (m_Enable4xMsaa)
        {
            sd.SampleDesc.Count = 4;
            sd.SampleDesc.Quality = m_4xMsaaQuality - 1;
        }
        else
        {
            sd.SampleDesc.Count = 1;
            sd.SampleDesc.Quality = 0;
        }
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 1;
        sd.OutputWindow = m_hMainWnd;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        sd.Flags = 0;
        HRB(dxgiFactory1->CreateSwapChain(m_pd3dDevice.Get(), &sd, m_pSwapChain.ReleaseAndGetAddressOf()));
    }

    // 可以禁止alt+enter全屏
    dxgiFactory1->MakeWindowAssociation(m_hMainWnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

    return true;
}