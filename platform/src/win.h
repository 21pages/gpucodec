#ifndef WIN_H
#define WIN_H

#include <dxgi.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_2.h>
#include <d3d11_3.h>
#include <d3d11_4.h>

#include <stdint.h>

class dx_device {
public:
	dx_device();
	~dx_device();
    HRESULT init(uint32_t adapterIdx);

private:
	/// D3D11 device context used for the operations demonstrated in this application
    ID3D11Device *pD3DDev = nullptr;
    /// D3D11 device context
    ID3D11DeviceContext *pCtx = nullptr;
};

#endif // WIN_H