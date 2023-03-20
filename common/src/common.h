#ifndef FFI_H
#define FFI_H

// #include "codec_id.h"
// #include "pixfmt.h"
// #include "hwcontext.h"

#include <stdint.h>

#define MAX_DATA_NUM    8


enum CodecID
{
    H264,
    HEVC,
    VP8,
    VP9,
    AV1,
};

enum HWDeviceType
{
    DX9,
    DX10,
    DX11,
    DX12,
    OPENCL,
    OPENGL,
    CUDA,
    VULKAN,
};

enum PixelFormat
{
    NV12,
    YUV420P,
};

typedef void (*EncodeCallback)(const uint8_t *data, int32_t len, int64_t pts, int32_t key, const void *obj);

typedef void (*DecodeCallback)(uint8_t *datas[MAX_DATA_NUM], int32_t linesizes[MAX_DATA_NUM], int32_t surfaceFormat,
                            int32_t width, int32_t height, const void *obj, int32_t key);



#endif