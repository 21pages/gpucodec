#ifndef AMF_FFI_H
#define AMF_FFI_H

#include "../../hw_common/src/callback.h"

int intel_driver_support();

void *intel_new_encoder(void *handle, int64_t luid, int32_t api,
                        int32_t dataFormat, int32_t width, int32_t height,
                        int32_t kbs, int32_t framerate, int32_t gop);

int intel_encode(void *encoder, void *tex, EncodeCallback callback, void *obj);

int intel_destroy_encoder(void *encoder);

void *intel_new_decoder(int64_t luid, int32_t api, int32_t dataFormat,
                        int32_t outputSurfaceFormat);

int intel_decode(void *decoder, uint8_t *data, int len, DecodeCallback callback,
                 void *obj);

int intel_destroy_decoder(void *decoder);

int intel_test_encode(void *outDescs, int32_t maxDescNum, int32_t *outDescNum,
                      int32_t api, int32_t dataFormat, int32_t width,
                      int32_t height, int32_t kbs, int32_t framerate,
                      int32_t gop);

int intel_test_decode(void *outDescs, int32_t maxDescNum, int32_t *outDescNum,
                      int32_t api, int32_t dataFormat,
                      int32_t outputSurfaceFormat, uint8_t *data,
                      int32_t length);

int intel_set_bitrate(void *encoder, int32_t kbs);

int intel_set_framerate(void *encoder, int32_t framerate);

#endif // AMF_FFI_H