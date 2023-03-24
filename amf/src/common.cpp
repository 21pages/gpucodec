#include <public/common/TraceAdapter.h>
#include "common.h"

#ifndef AMF_FACILITY
#define AMF_FACILITY        L"AMFCommon"
#endif

static bool convert_device(HWDeviceType lhs, amf::AMF_MEMORY_TYPE& rhs)
{
    switch (lhs)
    {
    case HOST:
        rhs = amf::AMF_MEMORY_HOST;
        break;
    case DX9:
        rhs = amf::AMF_MEMORY_DX9;
        break;
    case DX11:
        rhs = amf::AMF_MEMORY_DX11;
        break;
    case DX12:
        rhs = amf::AMF_MEMORY_DX12;
        break;
    case OPENCL:
        rhs = amf::AMF_MEMORY_OPENCL;
        break;
    case OPENGL:
        rhs = amf::AMF_MEMORY_OPENGL;
        break;
    case VULKAN:
        rhs = amf::AMF_MEMORY_VULKAN;
        break;
    default:
        std::cerr << "unsupported memory type: " << lhs << "\n";
        return false;
    }
    return true;
}

static bool convert_format(PixelFormat lhs, amf::AMF_SURFACE_FORMAT& rhs)
{
    switch (lhs)
    {
    case NV12:
        rhs = amf::AMF_SURFACE_NV12;
        break;
    default:
        std::cerr << "unsupported format: " << lhs << "\n";
        return false;
    }
    return true;
}
