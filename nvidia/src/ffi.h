#ifndef AMF_FFI_H
#define AMF_FFI_H

#include "../../common/src/callback.h"



void* nvidia_new_encoder(int32_t device, int32_t format, int32_t codecID, int32_t width, int32_t height, int32_t gpu);

int nvidia_encode(void *encoder,  uint8_t* datas[MAX_DATA_NUM], int32_t linesizes[MAX_DATA_NUM], EncodeCallback callback, void* obj);

int nvidia_destroy_encoder(void *encoder);

void* nvidia_new_decoder(int32_t device, int32_t format,int32_t codecID, int iGpu);

int nvidia_decode(void* decoder, uint8_t *data, int len, DecodeCallback callback, void* obj);

int nvidia_destroy_decoder(void* decoder);



#endif  // AMF_FFI_H