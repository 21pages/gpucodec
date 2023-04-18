#ifndef FFI_H
#define FFI_H

void* dxgi_new_duplicator();
void* dxgi_duplicate(void *self, int wait_ms);
void destroy_dxgi_duplicator(void *self);

#endif // FFI_H