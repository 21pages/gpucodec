#ifdef _WIN32
#include <d3d9.h>
#include <d3d11.h>
#endif

#include <public/common/AMFFactory.h>
#include <public/common/AMFSTL.h>
#include <public/common/ByteArray.h>
#include <public/common/TraceAdapter.h>
#include <public/common/Thread.h>
#include <public/include/components/VideoDecoderUVD.h>


#include <cstring>
#include <iostream>

#include "common.h"
#include "callback.h"

#define AMF_FACILITY        L"AMFDecoder"

class Decoder {
public:
    AMF_RESULT init_result = AMF_FAIL;
private:
    void *m_hdl;
    AMFFactoryHelper m_AMFFactory;
    amf::AMFContextPtr m_AMFContext = NULL;
    amf::AMFComponentPtr m_AMFDecoder = NULL;
    amf::AMF_MEMORY_TYPE m_memoryTypeOut;
    amf::AMF_SURFACE_FORMAT m_formatOut = amf::AMF_SURFACE_NV12;
    amf_wstring m_codec;

    // buffer
    std::vector<std::vector<uint8_t>> m_buffer;
public:
    Decoder(void *hdl, amf::AMF_MEMORY_TYPE memoryTypeOut, amf_wstring codec):
        m_hdl(hdl),
        m_memoryTypeOut(memoryTypeOut), m_codec(codec)
    {
        init_result = initialize();
    }

    AMF_RESULT decode(uint8_t *iData, uint32_t iDataSize, DecodeCallback callback, void *obj)
    {
        AMF_RESULT res = AMF_FAIL;
        bool decoded = false;
        amf::AMFBufferPtr iDataWrapBuffer = NULL;

        res = m_AMFContext->CreateBufferFromHostNative(iData, iDataSize, &iDataWrapBuffer, NULL);
        AMF_RETURN_IF_FAILED(res, L"AMF Failed to CreateBufferFromHostNative");
        res = m_AMFDecoder->SubmitInput(iDataWrapBuffer);
        AMF_RETURN_IF_FAILED(res, L"SubmitInput failed");
        amf::AMFDataPtr oData = NULL;
        do
        {
            res = m_AMFDecoder->QueryOutput(&oData);
            if (res == AMF_REPEAT)
            {
                amf_sleep(1);
            }
        } while (res == AMF_REPEAT);
        if (res == AMF_OK && oData != NULL)
        {
            amf::AMFSurfacePtr surface(oData);
            AMF_RETURN_IF_INVALID_POINTER(surface, L"surface is NULL");
            PixelFormat pixfmt;
            res = amf_pixfmt_to_common_pixfmt(surface->GetFormat(), pixfmt);

            
            void * native = surface->GetPlaneAt(0)->GetNative();

            switch (surface->GetMemoryType())
            {
                case amf::AMF_MEMORY_DX9:
                    {
                        IDirect3DSurface9* surfaceDX9 = (IDirect3DSurface9*)native;
                    }
                    
                    break;
                case amf::AMF_MEMORY_DX11:
                    {
                        ID3D11Texture2D* surfaceDX11 = (ID3D11Texture2D*)native;
                        D3D11_TEXTURE2D_DESC desc;
                        surfaceDX11->GetDesc(&desc);
                    }
                    break;
                case amf::AMF_MEMORY_OPENCL:
                {
                    uint8_t *buf = (uint8_t*)native;
                }
                    break;
                
            }
            #if 0
            std::cout << "DataType:" << surface->GetDataType() 
                       << " Format" << surface->GetFormat() 
                       << " FrameType" << surface->GetFrameType() 
                       << " MemoryType" << surface->GetMemoryType()
                       << std::endl;
            #endif


            decoded = true;
            surface = NULL;
        }
        oData = NULL;
        iDataWrapBuffer = NULL;
        return decoded ? AMF_OK : AMF_FAIL;
        return AMF_OK;
    }

    AMF_RESULT destroy()
    {
        if (m_AMFDecoder != NULL)
        {
            m_AMFDecoder->Terminate();
            m_AMFDecoder = NULL;
        }
        if (m_AMFContext != NULL)
        {
            m_AMFContext->Terminate();
            m_AMFContext = NULL; // context is the last
        }
        m_AMFFactory.Terminate();
        return AMF_OK;
    }
private:
    AMF_RESULT initialize()
    {
        AMF_RESULT res;

        res = m_AMFFactory.Init();
        if (res != AMF_OK) {
            std::cerr << "AMF init failed, error code = " <<  res << "\n";
            return res;
        }
        amf::AMFSetCustomTracer(m_AMFFactory.GetTrace());
        amf::AMFTraceEnableWriter(AMF_TRACE_WRITER_CONSOLE, true);
        amf::AMFTraceSetWriterLevel(AMF_TRACE_WRITER_CONSOLE, AMF_TRACE_WARNING);

        res = m_AMFFactory.GetFactory()->CreateContext(&m_AMFContext); 
        AMF_RETURN_IF_FAILED(res, L"AMF Failed to CreateContext");
        
        switch (m_memoryTypeOut)
        {
        case amf::AMF_MEMORY_DX9:
            res = m_AMFContext->InitDX9(NULL); // can be DX9 or DX9Ex device
            AMF_RETURN_IF_FAILED(res, L"AMF Failed to InitDX9");
            break;
        case amf::AMF_MEMORY_DX11:
            res = m_AMFContext->InitDX11(NULL); // can be DX11 device
            AMF_RETURN_IF_FAILED(res, L"AMF Failed to InitDX11");
            break;
        case amf::AMF_MEMORY_DX12:
            {
                amf::AMFContext2Ptr context2(m_AMFContext);
                if(context2 == nullptr)
                { 
                    AMFTraceError(AMF_FACILITY, L"amf::AMFContext2 is missing");
                    return AMF_FAIL;
                }
                res = context2->InitDX12(NULL); // can be DX11 device
                AMF_RETURN_IF_FAILED(res, L"AMF Failed to InitDX12");
            }
            break;
        case amf::AMF_MEMORY_VULKAN:
            res = amf::AMFContext1Ptr(m_AMFContext)->InitVulkan(NULL); // can be Vulkan device
            AMF_RETURN_IF_FAILED(res, L"AMF Failed to InitVulkan");
            break;
        default:
            break;
        }

        res = m_AMFFactory.GetFactory()->CreateComponent(m_AMFContext, m_codec.c_str(), &m_AMFDecoder);
        AMF_RETURN_IF_FAILED(res, L"AMF Failed to CreateComponent");

        res = setParameters();
        AMF_RETURN_IF_FAILED(res, L"AMF Failed to setParameters");

        res = m_AMFDecoder->Init(m_formatOut, 0, 0);
        AMF_RETURN_IF_FAILED(res, L"AMF Failed to Init decoder");

        return AMF_OK;
    }

    AMF_RESULT setParameters()
    {
        AMF_RESULT res;
        res = m_AMFDecoder->SetProperty(AMF_TIMESTAMP_MODE, amf_int64(AMF_TS_DECODE)); // our sample H264 parser provides decode order timestamps - change this depend on demuxer
        AMF_RETURN_IF_FAILED(res, L"SetProperty AMF_TIMESTAMP_MODE to AMF_TS_DECODE failed");
        res = m_AMFDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, amf_int64(AMF_VIDEO_DECODER_MODE_LOW_LATENCY));
        AMF_RETURN_IF_FAILED(res, L"SetProperty AMF_VIDEO_DECODER_REORDER_MODE to AMF_VIDEO_DECODER_MODE_LOW_LATENCY failed");
        return AMF_OK;
    }

    AMF_RESULT amf_pixfmt_to_common_pixfmt(amf::AMF_SURFACE_FORMAT lhs, PixelFormat &rhs)
    {
        switch (lhs)
        {
        case amf::AMF_SURFACE_NV12:
            rhs = NV12;
            break;
        default:
            return AMF_INVALID_FORMAT;
        }
        return AMF_OK;
    }
};

static bool convert_codec(DataFormat lhs, amf_wstring& rhs)
{
    switch (lhs)
    {
    case H264:
        rhs = AMFVideoDecoderUVD_H264_AVC;
        break;
    case H265:
        rhs = AMFVideoDecoderHW_H265_HEVC;
        break;
    case VP9:
        rhs = AMFVideoDecoderHW_VP9;
        break;
    case AV1:
        rhs = AMFVideoDecoderHW_AV1;
        break;
    default:
        std::cerr << "unsupported codec: " << lhs << "\n";
        return false;
    }
    return true;
}

#include "common.cpp"

extern "C" void* amf_new_decoder(void* hdl, HWDeviceType device, DataFormat dataFormat)
{
    try
    {
        amf_wstring codecStr;
        if (!convert_codec(dataFormat, codecStr))
        {
            return NULL;
        }
        amf::AMF_MEMORY_TYPE memoryTypeOut;
        if (!convert_device(device, memoryTypeOut))
        {
            return NULL;
        }
        Decoder *dec = new Decoder(hdl, memoryTypeOut, codecStr);
        if (dec && dec->init_result != AMF_OK)
        {
            dec->destroy();
            delete dec;
            dec = NULL;
        }
        return dec;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return NULL;
}

extern "C" int amf_decode(void *decoder, uint8_t *data, int32_t length, DecodeCallback callback, void *obj)
{
    try
    {
        Decoder *dec = (Decoder*)decoder;
        return dec->decode(data, length, callback, obj);   
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return -1;
}

extern "C" int amf_destroy_decoder(void *decoder)
{
    try
    {
        Decoder *dec = (Decoder*)decoder;
        if (dec)
        {
            return dec->destroy();
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return -1;
}