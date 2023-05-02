#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define MAX_DATA_NUM    8
#define MAX_GOP         0xFFFF


enum DataFormat
{
    H264,
    H265,
    VP8,
    VP9,
    AV1,
};

enum API
{
    API_DX11,
    API_OPENCL,
    API_OPENGL,
    API_VULKAN,
};

enum SurfaceFormat
{
    SURFACE_FORMAT_BGRA,
    SURFACE_FORMAT_RGBA,
    SURFACE_FORMAT_NV12,
};

enum Usage {
    ULTRA_LOW_LATENCY,
    LOW_LATENCY,
    LOW_LATENCY_HIGH_QUALITY,
};

enum Preset
{
    BALANCED,
    SPEED,
    QUALITY
};

#endif // COMMON_H