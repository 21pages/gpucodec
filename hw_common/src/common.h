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
    I420,
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