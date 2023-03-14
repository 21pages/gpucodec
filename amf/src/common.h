#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

enum Codec {
    H264,
    H265,
    AV1,
};

#define MAX_AV_PLANES   8

typedef void (*EncodeCallback)(const uint8_t *data,
                                int32_t len,
                                int64_t pts,    
                                int32_t key,
                                const void *obj);

typedef void (*DecodeCallback)(uint8_t *datas[MAX_AV_PLANES],
                                int32_t linesizes[MAX_AV_PLANES],
                                int32_t surfaceFormat,
                                int32_t width,
                                int32_t height,
                                const void *obj, 
                                int32_t key);

#endif // COMMON_H