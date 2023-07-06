#ifndef RENDER_FFI_H
#define RENDER_FFI_H

void *CreateDXGIRender(long long luid);
int DXGIRenderTexture(void *render, void *tex);
void DestroyDXGIRender(void *render);

#endif // RENDER_FFI_H