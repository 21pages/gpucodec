#ifndef DDA_H
#define DDA_H

#include "Defs.h"
#include "DDAImpl.h"

class DemoApplication
{
    /// Demo Application Core class
#define returnIfError(x)\
    if (FAILED(x))\
    {\
        printf(__FUNCTION__": Line %d, File %s Returning error 0x%08x\n", __LINE__, __FILE__, x);\
        return x;\
    }

private:
    /// DDA wrapper object, defined in DDAImpl.h
    DDAImpl *pDDAWrapper = nullptr;
    /// D3D11 device context used for the operations demonstrated in this application
    ID3D11Device *pD3DDev = nullptr;
    /// D3D11 device context
    ID3D11DeviceContext *pCtx = nullptr;
    /// D3D11 RGB Texture2D object that recieves the captured image from DDA
    ID3D11Texture2D *pDupTex2D = nullptr;
    /// D3D11 YUV420 Texture2D object that sends the image to NVENC for video encoding
    ID3D11Texture2D *pEncBuf = nullptr;
    ID3D10Multithread* hmt = NULL;
private:
/// Initialize DXGI pipeline
    HRESULT InitDXGI()
    {
        HRESULT hr = S_OK;
        /// Driver types supported
        D3D_DRIVER_TYPE DriverTypes[] =
        {
            D3D_DRIVER_TYPE_HARDWARE,
            // D3D_DRIVER_TYPE_WARP,
            // D3D_DRIVER_TYPE_REFERENCE,
        };
        UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

        /// Feature levels supported
        D3D_FEATURE_LEVEL FeatureLevels[] =
        {
            D3D_FEATURE_LEVEL_11_1,
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
            hr = D3D11CreateDevice(nullptr, DriverTypes[DriverTypeIndex], nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT, FeatureLevels, NumFeatureLevels,
                D3D11_SDK_VERSION, &pD3DDev, &FeatureLevel, &pCtx);
            if (SUCCEEDED(hr))
            {
                // Device creation succeeded, no need to loop anymore
                hr = pCtx->QueryInterface(IID_PPV_ARGS(&hmt));
                if (SUCCEEDED(hr)) {
                    hr = hmt->SetMultithreadProtected(TRUE);
                }
                break;
            }
        }
        return hr;
    }

    /// Initialize DDA handler
    HRESULT InitDup()
    {
        HRESULT hr = S_OK;
        if (!pDDAWrapper)
        {
            pDDAWrapper = new DDAImpl(pD3DDev, pCtx);
            hr = pDDAWrapper->Init();
            returnIfError(hr);
        }
        return hr;
    }

public:
    /// Initialize demo application
    // HRESULT Init(ID3D11Device *device, ID3D11DeviceContext *ctx)
    // {
    //     HRESULT hr = S_OK;

    //     pD3DDev = device;
    //     pD3DDev->AddRef();
    //     pCtx = ctx;
    //     pCtx->AddRef();

    //     hr = InitDup();
    //     returnIfError(hr);

    //     return hr;
    // }
    HRESULT Init()
    {
        HRESULT hr = S_OK;

        hr = InitDXGI();
        returnIfError(hr);

        hr = InitDup();
        returnIfError(hr);

        return hr;
    }

    ID3D11Device *Device() {
        return pD3DDev;
    }

    int width() { return pDDAWrapper->getWidth(); }

    int height() { return pDDAWrapper->getHeight(); }

    /// Capture a frame using DDA
    ID3D11Texture2D *Capture(int wait)
    {
        HRESULT hr = pDDAWrapper->GetCapturedFrame(&pDupTex2D, wait); // Release after preproc
        if (FAILED(hr))
        {
            return NULL;
        }
        return pDupTex2D;
    }

    /// Release all resources
    void Cleanup(bool bDelete = true)
    {
        if (pDDAWrapper)
        {
            pDDAWrapper->Cleanup();
            delete pDDAWrapper;
            pDDAWrapper = nullptr;
        }

        SAFE_RELEASE(pDupTex2D);
        if (bDelete)
        {
            SAFE_RELEASE(pD3DDev);
            SAFE_RELEASE(pCtx);
            SAFE_RELEASE(hmt)
        }
    }
    DemoApplication() {}
    ~DemoApplication()
    {
        Cleanup(true); 
    }
};

#endif // DDA_H