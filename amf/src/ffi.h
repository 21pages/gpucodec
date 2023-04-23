#ifndef AMF_FFI_H
#define AMF_FFI_H

#include "../../hw_common/src/callback.h"

int amf_driver_support();

void* amf_new_encoder(void* hdl, int32_t device, int32_t codecID, 
                        int32_t width, int32_t height, 
                        int32_t bitrate, int32_t framerate, int32_t gop);

int amf_encode(void *self, void *tex, EncodeCallback callback, void* obj);

int amf_destroy_encoder(void *self);

void* amf_new_decoder(void* hdl, int32_t device, int32_t codecID);

int amf_decode(void *decoder, uint8_t *data, int32_t length, DecodeCallback callback, void *obj);

int amf_destroy_decoder(void *decoder);

int amf_set_bitrate(void *e, int32_t bitrate);

int amf_set_framerate(void *e, int32_t framerate);

#endif // AMF_FFI_H