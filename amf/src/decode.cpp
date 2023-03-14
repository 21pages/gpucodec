#include <public/common/AMFFactory.h>
#include <public/common/AMFSTL.h>
#include <public/common/ByteArray.h>
#include <public/common/TraceAdapter.h>
#include <public/common/Thread.h>
#include <public/include/components/VideoDecoderUVD.h>


#include <cstring>

#include "common.h"

#define AMF_FACILITY        L"AMFDecoder"

class Decoder {
public:
    AMF_RESULT init_result = AMF_FAIL;
private:
    AMFFactoryHelper m_AMFFactory;
    amf::AMFContextPtr m_AMFContext = NULL;
    amf::AMFComponentPtr m_AMFDecoder = NULL;
    amf::AMF_MEMORY_TYPE m_memoryTypeOut;
    amf::AMF_SURFACE_FORMAT m_formatOut;
    amf_wstring m_codec;

    // buffer
    std::vector<std::vector<uint8_t>> m_buffer;
public:
    Decoder(amf::AMF_MEMORY_TYPE memoryTypeOut, amf::AMF_SURFACE_FORMAT formatOut, amf_wstring codec):
        m_memoryTypeOut(memoryTypeOut), m_formatOut(formatOut), m_codec(codec)
    {
        init_result = initialize();
    }
    
    ~Decoder()
    {
        AMFTraceDebug(AMF_FACILITY, L"~Decoder()");
    }

    AMF_RESULT decode(uint8_t *iData, uint32_t iDataSize, DecodeCallback callback, void *obj)
    {
        AMF_RESULT res;
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
            res = oData->Convert(amf::AMF_MEMORY_HOST); // slow
            AMF_RETURN_IF_FAILED(res, L"Convert failed");
            amf::AMFSurfacePtr surface(oData);
            AMF_RETURN_IF_INVALID_POINTER(surface, L"surface is NULL");
            amf_size count = surface->GetPlanesCount();
            amf::AMF_SURFACE_FORMAT format =  surface->GetFormat();
            uint8_t * datas[MAX_AV_PLANES] = {NULL};
            uint32_t linesizes[MAX_AV_PLANES] = {0};
            int y_width = 0, y_height = 0;
            m_buffer.resize(count);
            // Plane's width, height, linesize is different from ffmepg
            for (amf_size i = 0; i < count && i < MAX_AV_PLANES; i++)
            {
                amf::AMFPlanePtr plane = surface->GetPlaneAt(i);
                if (i == 0)
                {
                    y_width = plane->GetWidth();
                    y_height = plane->GetHeight();
                }
                // write surface removing offsets and alignments
                amf_uint8 *data     = reinterpret_cast<amf_uint8*>(plane->GetNative());
                amf_int32 offsetX   = plane->GetOffsetX();
                amf_int32 offsetY   = plane->GetOffsetY();              //  Y       UV
                amf_int32 pixelSize = plane->GetPixelSizeInBytes();     //  1       2
                amf_int32 height    = plane->GetHeight();               // 1800     900
                amf_int32 width     = plane->GetWidth();                // 2880     1800 
                amf_int32 pitchH    = plane->GetHPitch();               // 3072     3072

                m_buffer[i].resize(pixelSize * width * height);
                for( amf_int32 y = 0; y < height; y++)
                {
                    amf_uint8 *line = data + (y + offsetY) * pitchH;
                    std::memcpy(&m_buffer[i][pixelSize * width * y], line, pixelSize * width);
                }
                datas[i] = m_buffer[i].data();
                linesizes[i] = pixelSize * width;
            }
            callback(datas, linesizes, format, y_width, y_height, obj, 0);
            surface = NULL;
        }
        oData = NULL;
        iDataWrapBuffer = NULL;
        return res;
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
        AMF_RETURN_IF_FAILED(res, L"AMF Failed to initialize");

        m_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_DEBUG_OUTPUT, AMF_TRACE_TRACE);

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
};

extern "C" Decoder* amf_new_decoder(amf::AMF_MEMORY_TYPE memoryTypeOut, amf::AMF_SURFACE_FORMAT formatOut, Codec codec)
{
    amf_wstring codecStr;
    switch (codec)
    {
    case H264:
        codecStr = AMFVideoDecoderUVD_H264_AVC;
        break;
    case H265:
        codecStr = AMFVideoDecoderHW_H265_HEVC;
        break;
    case AV1:
        codecStr = AMFVideoDecoderHW_AV1;
        break;
    }
    Decoder *dec = new Decoder(memoryTypeOut, formatOut, codecStr);
    if (dec && dec->init_result != AMF_OK)
    {
        dec->destroy();
        delete dec;
        dec = NULL;
    }
    return dec;
}

extern "C" int amf_decode(Decoder *dec, uint8_t *data, uint32_t length, DecodeCallback callback, void *obj)
{
    return dec->decode(data, length, callback, obj);
}

extern "C" int amf_destroy_decoder(Decoder *dec)
{
    return dec->destroy();
}