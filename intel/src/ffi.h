#ifndef AMF_FFI_H
#define AMF_FFI_H

#include "../../hw_common/src/callback.h"

int intel_driver_support();

void* intel_new_encoder(int32_t device, int32_t format, int32_t codecID,
                        int32_t width, int32_t height, 
                        int32_t bitrate, int32_t framerate, int32_t gop,
                        int32_t pitchs[MAX_DATA_NUM]);

int intel_encode(void *encoder,  uint8_t* datas[MAX_DATA_NUM], int32_t linesizes[MAX_DATA_NUM], EncodeCallback callback, void* obj);

int intel_destroy_encoder(void *encoder);

void* intel_new_decoder(int32_t device, int32_t format,int32_t codecID);

int intel_decode(void* decoder, uint8_t *data, int len, DecodeCallback callback, void* obj);

int intel_destroy_decoder(void* decoder);

int intel_set_bitrate(void *e, int32_t bitrate);

int intel_set_framerate(void *e, int32_t framerate);



#endif  // AMF_FFI_H