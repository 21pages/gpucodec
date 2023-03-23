#ifndef COMMON_H
#define COMMON_H

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
    HOST,
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


#endif // COMMON_H