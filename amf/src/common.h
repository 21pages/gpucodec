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
                                int len,
                                int64_t pts,    
                                int key,
                                const void *obj);

typedef void (*DecodeCallback)(uint8_t *datas[MAX_AV_PLANES],
                                uint32_t linesizes[MAX_AV_PLANES],
                                int surfaceFormat,
                                int width,
                                int height,
                                const void *obj, 
                                int key);

#endif // COMMON_H