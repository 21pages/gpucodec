#ifndef WIN_H
#define WIN_H

#include <iostream>

#include <d3d11.h>
#include <d3d11_1.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#define SAFE_RELEASE(p) { if ((p)) { (p)->Release(); (p) = nullptr; } }
#define HRB(hr) { if (FAILED((hr))) { std::cerr << "HR = " << (hr); return false; } }

enum class AdapterVendor {
    ANY = 0,
    AMD = 0x1002,
    INTEL = 0x8086,
    NVIDIA = 0x10DE,
};

class Texture_Lifetime_Keeper {
public:
    Texture_Lifetime_Keeper(void *texture) {
        d3d11_texture.Attach((ID3D11Texture2D*)texture);
    }
private:
    ComPtr<ID3D11Texture2D> d3d11_texture;
};

class NativeDevice {
public:
    bool Init(AdapterVendor vendor);
public:
    // Direct3D 11
    ComPtr<IDXGIFactory1> m_DXGIFactory1;
    ComPtr<IDXGIAdapter1> m_DXGIAdapter1;
    ComPtr<ID3D11Device> m_pd3dDevice;
    ComPtr<ID3D11DeviceContext> m_pd3dImmediateContext;
    ComPtr<IDXGISwapChain> m_pSwapChain;
     // Direct3D 11.1
    ComPtr<IDXGIFactory2> m_DXGIFactory2;
    ComPtr<IDXGIAdapter2> m_DXGIAdapter2;
    ComPtr<ID3D11Device1> m_pd3dDevice1;
    ComPtr<ID3D11DeviceContext1> m_pd3dImmediateContext1;
    ComPtr<IDXGISwapChain1> m_pSwapChain1;
     // resource
    ComPtr<ID3D11Texture2D> m_pDepthStencilBuffer;
    ComPtr<ID3D11RenderTargetView> m_pRenderTargetView;
    ComPtr<ID3D11DepthStencilView> m_pDepthStencilView;
    D3D11_VIEWPORT m_ScreenViewport;

    HINSTANCE m_hAppInst;        // 应用实例句柄
    HWND      m_hMainWnd;        // 主窗口句柄
    bool      m_AppPaused;       // 应用是否暂停
    bool      m_Minimized;       // 应用是否最小化
    bool      m_Maximized;       // 应用是否最大化
    bool      m_Resizing;        // 窗口大小是否变化
    bool      m_Enable4xMsaa;    // 是否开启4倍多重采样
    UINT      m_4xMsaaQuality;   // MSAA支持的质量等级

    // 派生类应该在构造函数设置好这些自定义的初始参数
    std::wstring m_MainWndCaption;                       // 主窗口标题
    int m_ClientWidth;                                   // 视口宽度
    int m_ClientHeight;                                  // 视口高度
};

#endif