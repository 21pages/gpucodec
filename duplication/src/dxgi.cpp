#include <DDA.h>

extern "C" void* dxgi_new_duplicator()
{
    DemoApplication *d = new DemoApplication();
    HRESULT hr = d->Init();
    if (FAILED(hr)) {
        delete d;
        d = NULL;
        return NULL;
    }

    return d;
}

extern "C" void* dxgi_device(void *self) {
    DemoApplication *d = (DemoApplication *)self;
    return d->Device();
}

extern "C" void* dxgi_duplicate(void *self, int wait_ms)
{
    DemoApplication *d = (DemoApplication *)self;
    return d->Capture(wait_ms);
}

extern "C" void destroy_dxgi_duplicator(void *self)
{
    DemoApplication *d = (DemoApplication *)self;
    if (d) delete d;
}