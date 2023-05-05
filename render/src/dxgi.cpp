#include <thread>
#include ""

class Render {
public:
    LRESULT Init();
    int RenderTexture(ID3D11Texture2D*);
private:
    std::unique_ptr<std::thread> message_thread;
};


extern "C" void* CreateDXGIRender()
{
    Render *p = new Render();
    p->Init();
    return p;
}

extern "C" int DXGIRenderTexture(void *render, void *tex)
{
    Render *self = (Render*)render;
    self->RenderTexture((ID3D11Texture2D*)tex);
    return 0;
}

extern "C" void DestroyDXGIRender(void *render)
{
    Render *p = new Render();
}

extern "C" void* DXGIGetDevice(void *render)
{
    Render *self = (Render*)render;
    return NULL;
}

