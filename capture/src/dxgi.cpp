#include <DDA.h>
#include <Windows.h>
#include <string>

extern "C" void *dxgi_new_capturer() {
  DemoApplication *d = new DemoApplication();
  HRESULT hr = d->Init();
  if (FAILED(hr)) {
    delete d;
    d = NULL;
    return NULL;
  }

  return d;
}

extern "C" void *dxgi_device(void *capturer) {
  DemoApplication *d = (DemoApplication *)capturer;
  return d->Device();
}

extern "C" void *dxgi_capture(void *capturer, int wait_ms) {
  DemoApplication *d = (DemoApplication *)capturer;
  void *texture = d->Capture(wait_ms);
  return texture;
}

extern "C" void destroy_dxgi_capturer(void *capturer) {
  DemoApplication *d = (DemoApplication *)capturer;
  if (d)
    delete d;
}