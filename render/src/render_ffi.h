#ifndef RENDER_FFI_H
#define RENDER_FFI_H

void* CreateDXGIRender();
int DXGIRenderTexture(void *render, void *tex);
void DestroyDXGIRender(void *render);
void* DXGIGetDevice(void *render);

#endif // RENDER_FFI_H