#ifndef AMF_FFI_H
#define AMF_FFI_H

#include "../../hw_common/src/callback.h"

int amf_driver_support();

void* amf_new_encoder(void* hdl, int32_t api, int32_t data_format, 
                        int32_t width, int32_t height, 
                        int32_t bitrate, int32_t framerate, int32_t gop);

int amf_encode(void *self, void *tex, EncodeCallback callback, void* obj);

int amf_destroy_encoder(void *self);

void* amf_new_decoder(int64_t luid, int32_t api, int32_t dataFormat, int32_t outputSurfaceFormat);

int amf_decode(void *decoder, uint8_t *data, int32_t length, DecodeCallback callback, void *obj);

int amf_destroy_decoder(void *decoder);

int amf_test_encode(void *outDescs, int32_t maxDescNum, int32_t *outDescNum,
                    int32_t api, int32_t dataFormat,
                    int32_t width, int32_t height,
                    int32_t kbs, int32_t framerate, int32_t gop);

int amf_test_decode(void *outDescs, int32_t maxDescNum, int32_t *outDescNum, 
                    int32_t api, int32_t dataFormat, int32_t outputSurfaceFormat,
                    uint8_t *data, int32_t length);

int amf_set_bitrate(void *e, int32_t bitrate);

int amf_set_framerate(void *e, int32_t framerate);

#endif // AMF_FFI_H