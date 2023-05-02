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