#ifndef WIN_H
#define WIN_H

#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

class Texture_Lifetime_Keeper {
public:
    Texture_Lifetime_Keeper(void *texture) {
        d3d11_texture.Attach((ID3D11Texture2D*)texture);
    }
private:
    ComPtr<ID3D11Texture2D> d3d11_texture;
};

#endif