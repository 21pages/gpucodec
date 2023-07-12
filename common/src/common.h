#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define MAX_DATA_NUM 8
#define MAX_GOP 0xFFFF

enum AdapterVendor {
  ADAPTER_VENDOR_AMD = 0x1002,
  ADAPTER_VENDOR_INTEL = 0x8086,
  ADAPTER_VENDOR_NVIDIA = 0x10DE,
};

enum DataFormat {
  H264,
  H265,
  VP8,
  VP9,
  AV1,
};

enum API {
  API_DX11,
  API_OPENCL,
  API_OPENGL,
  API_VULKAN,
};

enum SurfaceFormat {
  SURFACE_FORMAT_BGRA,
  SURFACE_FORMAT_RGBA,
  SURFACE_FORMAT_NV12,
};

enum Usage {
  ULTRA_LOW_LATENCY,
  LOW_LATENCY,
  LOW_LATENCY_HIGH_QUALITY,
};

enum Preset { BALANCED, SPEED, QUALITY };

struct AdapterDesc {
  int64_t luid;
};

#endif // COMMON_H