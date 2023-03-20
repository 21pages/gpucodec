#ifndef AMF_FFI_H
#define AMF_FFI_H

#include "../../common/src/callback.h"



void* nvidia_new_encoder(int32_t width, int32_t height, int codecID, uint32_t format, int32_t gpu);

int nvidia_encode(void *e,  uint8_t *data, int32_t len, EncodeCallback callback, void* obj);

int nvidia_destroy_encoder(void *e);

void* nvidia_new_decoder(int avCodecID, int iGpu);

int nvidia_decode(void* decoder, uint8_t *data, int len, DecodeCallback callback, void* obj);

int nvidia_destroy_decoder(void* decoder);



#endif  // AMF_FFI_H