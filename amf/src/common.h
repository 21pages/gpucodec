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

#endif // COMMON_H