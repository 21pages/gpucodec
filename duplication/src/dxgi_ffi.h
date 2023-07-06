#ifndef FFI_H
#define FFI_H

void *dxgi_new_duplicator();
void *dxgi_device(void *dup);
void *dxgi_duplicate(void *dup, int wait_ms);
void destroy_dxgi_duplicator(void *dup);

#endif // FFI_H