#include <string.h>
#include "win.h"

using namespace std;

dx_device::dx_device()
{
}

dx_device::~dx_device()
{
    if (pD3DDev) pD3DDev->Release();
    if (pCtx) pCtx->Release();
}

#include <initguid.h>
DEFINE_GUID(DXGI_DEBUG_D3D11, 0x4b99317b, 0xac39, 0x4aa6, 0xbb, 0xb, 0xba, 0xa0, 0x47, 0x84, 0x79, 0x8f);

HRESULT dx_device::init(uint32_t adapterIdx) {
    // // factory
    // HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory1));
    // if (FAILED(hr)) return hr;
    // // adapter
    // HRESULT hr = factory1->EnumAdapters1(adapterIdx, &adapter1);
    // if (FAILED(hr)) return hr;
    // // device
	// DXGI_ADAPTER_DESC desc;
	// D3D_FEATURE_LEVEL levelUsed = D3D_FEATURE_LEVEL_10_0;
	// HRESULT hr = 0;
	// adpIdx = adapterIdx;

	// uint32_t createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    // std::wstring adapterName = (adapter1->GetDesc(&desc) == S_OK) ? desc.Description
	// 						: L"<unknown>";
    // D3D_FEATURE_LEVEL featureLevels[] = {
    //     D3D_FEATURE_LEVEL_11_0,
    //     D3D_FEATURE_LEVEL_10_1,
    //     D3D_FEATURE_LEVEL_10_0,
    // };
    // hr = D3D11CreateDevice(adapter1, D3D_DRIVER_TYPE_UNKNOWN, NULL,
	// 		       createFlags, featureLevels,
	// 		       sizeof(featureLevels) /
	// 			       sizeof(D3D_FEATURE_LEVEL),
	// 		       D3D11_SDK_VERSION, &device, &levelUsed,
	// 		       &context);
	// if (FAILED(hr)) return hr;

    HRESULT hr = S_OK;
    /// Driver types supported
    D3D_DRIVER_TYPE DriverTypes[] =
    {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };
    UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

    /// Feature levels supported
    D3D_FEATURE_LEVEL FeatureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_1
    };
    UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);
    D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_11_0;

    /// Create device
    for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
    {
        hr = D3D11CreateDevice(nullptr, DriverTypes[DriverTypeIndex], nullptr, /*D3D11_CREATE_DEVICE_DEBUG*/0, FeatureLevels, NumFeatureLevels,
            D3D11_SDK_VERSION, &pD3DDev, &FeatureLevel, &pCtx);
        if (SUCCEEDED(hr))
        {
            // Device creation succeeded, no need to loop anymore
            break;
        }
    }
    return hr;
    
}

ID3D11Device *get_device()


