#ifndef CALLBACK_H
#define CALLBACK_H

#include <stdint.h>

#define MAX_DATA_NUM 8

typedef void (*EncodeCallback)(const uint8_t *data, int32_t len, int64_t pts,
                               int32_t key, const void *obj);

typedef void (*DecodeCallback)(void *texture, int32_t surfaceFormat,
                               int32_t width, int32_t height, const void *obj,
                               int32_t key);

#endif // CALLBACK_H