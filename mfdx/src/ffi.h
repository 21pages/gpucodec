#ifndef AMF_FFI_H
#define AMF_FFI_H

#include "../../common/src/callback.h"
#include <stdbool.h>

void *mfdx_new_decoder(void *device, int64_t luid, int32_t api, int32_t codecID,
                       bool outputSharedHandle);

int mfdx_decode(void *decoder, uint8_t *data, int len, DecodeCallback callback,
                void *obj);

int mfdx_destroy_decoder(void *decoder);

int mfdx_test_decode(void *outDescs, int32_t maxDescNum, int32_t *outDescNum,
                     int32_t api, int32_t dataFormat, bool outputSharedHandle,
                     uint8_t *data, int32_t length);

#endif // AMF_FFI_H