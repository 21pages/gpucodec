#include <stdio.h>
#include <public/common/AMFFactory.h>
#include <public/common/Thread.h>
#include <public/common/AMFSTL.h>
#include <public/common/TraceAdapter.h>
#include <public/include/core/Platform.h>
#include <public/include/components/VideoEncoderVCE.h>
#include <public/include/components/VideoEncoderHEVC.h>
#include <public/include/components/VideoEncoderAV1.h>

#include <iostream>
#include <math.h>
#include <cstring>

#include "system.h"
#include "common.h"
#include "callback.h"

#define AMF_FACILITY        L"AMFEncoder"
#define MILLISEC_TIME       10000

/** Encoder output packet */
struct encoder_packet {
	uint8_t *data; /**< Packet data */
	size_t size;   /**< Packet size */

	int64_t pts; /**< Presentation timestamp */
	int64_t dts; /**< Decode timestamp */

	int32_t timebase_num; /**< Timebase numerator */
	int32_t timebase_den; /**< Timebase denominator */

	bool keyframe; /**< Is a keyframe */

	/* ---------------------------------------------------------------- */
	/* Internal video variables (will be parsed automatically) */

	/* DTS in microseconds */
	int64_t dts_usec;

	/* System DTS in microseconds */
	int64_t sys_dts_usec;
};


class Encoder {

public:
    AMF_RESULT init_result = AMF_FAIL;
    DataFormat m_dataFormat;
    amf::AMFComponentPtr m_AMFEncoder = NULL;
    amf::AMFContextPtr m_AMFContext = NULL;
private:
    // system
    void *m_hdl;
    // AMF Internals
    AMFFactoryHelper m_AMFFactory;
    amf::AMFComputePtr m_AMFCompute = NULL;
    amf::AMF_MEMORY_TYPE    m_AMFMemoryType;
    amf::AMF_SURFACE_FORMAT m_AMFSurfaceFormat = amf::AMF_SURFACE_BGRA;
    std::pair<int32_t, int32_t> m_Resolution;
    amf_wstring m_codec;
    bool m_OpenCLSubmission = false; // Possible memory leak
    // const
    AMF_COLOR_BIT_DEPTH_ENUM m_eDepth = AMF_COLOR_BIT_DEPTH_8;
    int m_query_timeout = 500;
    int32_t m_bitRateIn;
    int32_t m_frameRate;
    int32_t m_gop;

    // Buffers
	std::vector<uint8_t> m_PacketDataBuffer;

public:
    Encoder(void* hdl, amf::AMF_MEMORY_TYPE memoryType, amf_wstring codec, 
            DataFormat dataFormat,int32_t width, int32_t height, 
            int32_t bitrate, int32_t framerate, int32_t gop)
    {
        m_hdl = hdl;
        m_dataFormat = dataFormat;
        m_AMFMemoryType = memoryType;
        m_Resolution = std::make_pair(width, height);
        m_codec = codec;
        m_bitRateIn = bitrate;
        m_frameRate = framerate;
        m_gop = gop;
        init_result = initialize();
    }

    AMF_RESULT encode(void* tex, EncodeCallback callback, void* obj)
    {
        amf::AMFSurfacePtr surface = NULL;
        amf::AMFComputeSyncPointPtr pSyncPoint = NULL;
        AMF_RESULT res;
        bool encoded = false;
        Texture_Lifetime_Keeper keeper(tex);
        
        switch (m_AMFMemoryType)
        {
        case amf::AMF_MEMORY_DX11:
            res = m_AMFContext->CreateSurfaceFromDX11Native(tex, &surface, NULL);
            AMF_RETURN_IF_FAILED(res, L"CreateSurfaceFromDX11Native() failed");
            break;
        default:
            break;
        }
        res = m_AMFEncoder->SubmitInput(surface);
        AMF_RETURN_IF_FAILED(res, L"SubmitInput() failed");

        amf::AMFDataPtr data = NULL;
        do
        {
            res = m_AMFEncoder->QueryOutput(&data);
            if (res == AMF_REPEAT)
            {
                amf_sleep(1);
            }
        } while (res == AMF_REPEAT);
        if (res == AMF_OK && data != NULL)
        {
                struct encoder_packet packet;
                PacketKeyframe(data, &packet);
                amf::AMFBufferPtr pBuffer   = amf::AMFBufferPtr(data);
                packet.size = pBuffer->GetSize();
                if (packet.size > 0) {
                    if (m_PacketDataBuffer.size() < packet.size) {
                        size_t newBufferSize = (size_t)exp2(ceil(log2((double)packet.size)));
                        m_PacketDataBuffer.resize(newBufferSize);
                    }
                    packet.data = m_PacketDataBuffer.data();
                    std::memcpy(packet.data, pBuffer->GetNative(), packet.size);
                    if (callback) callback(packet.data, packet.size, 0, packet.keyframe, obj);
                    encoded = true;
                }
                pBuffer = NULL;
        }
        data = NULL;
        pSyncPoint = NULL;
        surface = NULL;
        return encoded ? AMF_OK : AMF_FAIL;
    }

    AMF_RESULT destroy()
    {
        if (m_AMFCompute)
        {
            m_AMFCompute = NULL;
        }
        if (m_AMFEncoder)
        {
            m_AMFEncoder->Terminate();
            m_AMFEncoder = NULL;
        }
        if (m_AMFContext)
        {
            m_AMFContext->Terminate();
            m_AMFContext = NULL; // m_AMFContext is the last
        }
        m_AMFFactory.Terminate();
        return AMF_OK;
    }

    AMF_RESULT test()
    {
        AMF_RESULT res = AMF_OK;
        amf::AMFSurfacePtr surface = nullptr;
        res = m_AMFContext->AllocSurface(m_AMFMemoryType, m_AMFSurfaceFormat, m_Resolution.first, m_Resolution.second, &surface);
        AMF_RETURN_IF_FAILED(res, L"AllocSurface() failed");
        if (surface->GetPlanesCount() < 1) return AMF_FAIL;
        void * native = surface->GetPlaneAt(0)->GetNative();
        if (!native) return AMF_FAIL;
        return encode(native, nullptr, nullptr);
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

        // m_AMFContext
        res = m_AMFFactory.GetFactory()->CreateContext(&m_AMFContext);
        AMF_RETURN_IF_FAILED(res, L"CreateContext() failed");

        switch (m_AMFMemoryType)
        {
        #ifdef _WIN32
        case amf::AMF_MEMORY_DX11:
            res = m_AMFContext->InitDX11(m_hdl); // can be DX11 device
            AMF_RETURN_IF_FAILED(res, L"InitDX11(m_hdl) failed");
            m_OpenCLSubmission = true;
            break;
        #endif
        case amf::AMF_MEMORY_VULKAN:
            res = amf::AMFContext1Ptr(m_AMFContext)->InitVulkan(m_hdl);
            AMF_RETURN_IF_FAILED(res, L"InitVulkan(NULL) failed");
            break;
        case amf::AMF_MEMORY_OPENCL:
            res = m_AMFContext->InitOpenCL(NULL);
            AMF_RETURN_IF_FAILED(res, L"InitOpenCL(NULL) failed");
            break;
        default:
            AMFTraceInfo(AMF_FACILITY, L"no init operation\n");
            break;
        }
        if (m_OpenCLSubmission)
        {
            if (m_AMFMemoryType != amf::AMF_MEMORY_OPENCL)
            {
                res = m_AMFContext->InitOpenCL(NULL);
                AMF_RETURN_IF_FAILED(res, L"InitOpenCL(NULL) failed");
            }
            res = m_AMFContext->GetCompute(amf::AMF_MEMORY_OPENCL, &m_AMFCompute);
            AMF_RETURN_IF_FAILED(res, L"OPENCL GetCompute failed, memoryType:%d", m_AMFMemoryType);
        } 
        else if (amf::AMF_MEMORY_DX11 == m_AMFMemoryType ||
            amf::AMF_MEMORY_VULKAN == m_AMFMemoryType ||
            amf::AMF_MEMORY_OPENCL == m_AMFMemoryType )
        {
            res = m_AMFContext->GetCompute(m_AMFMemoryType, &m_AMFCompute);
            AMF_RETURN_IF_FAILED(res, L"GetCompute failed, memoryType:%d", m_AMFMemoryType);
        }

        // component: encoder
        res = m_AMFFactory.GetFactory()->CreateComponent(m_AMFContext, m_codec.c_str(), &m_AMFEncoder);
        AMF_RETURN_IF_FAILED(res, L"CreateComponent(%s) failed", m_codec.c_str());

        res = SetParams(m_codec);
        AMF_RETURN_IF_FAILED(res, L"Could not set params in encoder.");

        res = m_AMFEncoder->Init(m_AMFSurfaceFormat, m_Resolution.first, m_Resolution.second);
        AMF_RETURN_IF_FAILED(res, L"encoder->Init() failed");

        return AMF_OK;
    }

    AMF_RESULT SetParams(const  amf_wstring& codecStr)
    {
        AMF_RESULT res;
        if (codecStr == amf_wstring(AMFVideoEncoderVCE_AVC))
        {
            // ------------- Encoder params usage---------------
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY) failed");

            // ------------- Encoder params static---------------
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(m_Resolution.first, m_Resolution.second));
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, %dx%d) failed", m_Resolution.first, m_Resolution.second);
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true);
            AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true) failed");
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED) failed");
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_COLOR_BIT_DEPTH, m_eDepth);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_COLOR_BIT_DEPTH, %d) failed", m_eDepth);

            // ------------- Encoder params dynamic ---------------
            m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0);
            // do not check error for AMF_VIDEO_ENCODER_B_PIC_PATTERN - can be not supported - check Capability Manager sample
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_QUERY_TIMEOUT, m_query_timeout); //ms
            AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_QUERY_TIMEOUT, %d) failed", m_query_timeout);
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, m_bitRateIn);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, %" LPRId64 L") failed", m_bitRateIn);
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, ::AMFConstructRate(m_frameRate, 1));
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, %d) failed", m_frameRate);
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_IDR_PERIOD, m_gop);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_IDR_PERIOD, %d) failed", m_gop);
        }
        else if (codecStr == amf_wstring(AMFVideoEncoder_HEVC))
        {
            // ------------- Encoder params usage---------------
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_LOW_LATENCY);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCODING)");

            // ------------- Encoder params static---------------
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, ::AMFConstructSize(m_Resolution.first, m_Resolution.second));
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, %dx%d) failed", m_Resolution.first, m_Resolution.second);
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_LOWLATENCY_MODE, true);
            AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true) failed");
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED)");
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH, m_eDepth);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH, %d) failed", m_eDepth);

            // ------------- Encoder params dynamic ---------------
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUERY_TIMEOUT, m_query_timeout); //ms
            AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUERY_TIMEOUT, %d) failed", m_query_timeout);
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, m_bitRateIn);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, %" LPRId64 L") failed", m_bitRateIn);
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, ::AMFConstructRate(m_frameRate, 1));
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, %d) failed", m_frameRate);
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_GOP_SIZE, m_gop); // todo
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_GOP_SIZE, %d) failed", m_gop);
        } 
        else {
            return AMF_FAIL;
        }
        return AMF_OK;
    }

    void PacketKeyframe(amf::AMFDataPtr& pData, struct encoder_packet* packet)
    {
        if (AMFVideoEncoderVCE_AVC == m_codec)
        {
            uint64_t pktType;
            pData->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE, &pktType);
            packet->keyframe = AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR == pktType || AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_I == pktType;
        }
        else if (AMFVideoEncoder_HEVC == m_codec)
        {
            uint64_t pktType;
            pData->GetProperty(AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE, &pktType);
            packet->keyframe = AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_IDR == pktType || AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_I == pktType;
        }
    }
};

static bool convert_codec(DataFormat lhs, amf_wstring& rhs)
{
    switch (lhs)
    {
    case H264:
        rhs = AMFVideoEncoderVCE_AVC;
        break;
    case H265:
        rhs = AMFVideoEncoder_HEVC;
        break;
    default:
        std::cerr << "unsupported codec: " << lhs << "\n";
        return false;
    }
    return true;
}

#include "common.cpp"

extern "C" void* amf_new_encoder(void* hdl, API api, DataFormat dataFormat,
                                int32_t width, int32_t height,
                                int32_t kbs, int32_t framerate, int32_t gop) 
{
    try 
    {
        if (width % 2 != 0 || height % 2 != 0)
        {
            return NULL;
        }
        amf_wstring codecStr;
        if (!convert_codec(dataFormat, codecStr))
        {
            return NULL;
        }
        amf::AMF_MEMORY_TYPE memoryType;
        if (!convert_api(api, memoryType))
        {
            return NULL;
        }
        Encoder *enc = new Encoder(hdl, memoryType, codecStr, dataFormat,
                                    width, height,
                                    kbs * 1000, framerate, gop);
        if (enc && enc->init_result != AMF_OK) {
            enc->destroy();
            delete enc; // TODO: run all in non-amf crash
            enc = NULL;
        }
        return enc;
    } 
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return NULL;
}

extern "C" int amf_encode(void *e, void *tex, EncodeCallback callback, void* obj)
{
    try
    {
        Encoder *enc = (Encoder*)e;
        return enc->encode(tex, callback, obj);
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return -1;
}

extern "C" int amf_destroy_encoder(void *e)
{
    try
    {
        Encoder *enc = (Encoder*)e;
        return enc->destroy();
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return -1;
}

extern "C" int amf_driver_support()
{
    try
    {
        AMFFactoryHelper factory;        
        AMF_RESULT res = factory.Init();
        if (res == AMF_OK)
        {
            factory.Terminate();
            return 0;
        }
    }
    catch(const std::exception& e)
    {
    }
    return -1;
}

extern "C" int amf_test_encode(void *encoder) {
    try
    {
        Encoder *self = (Encoder*)encoder;
        return self->test() == AMF_OK ? 0 : -1;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return -1;
}

extern "C" int amf_set_bitrate(void *e, int32_t bitrate)
{
    try
    {
        Encoder *enc = (Encoder*)e;
        AMF_RESULT res = AMF_FAIL;
        switch (enc->m_dataFormat)
        {
        case H264:
            res = enc->m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, bitrate);
            break;
        case H265:
            res = enc->m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, bitrate);
            break;
        }
        return res == AMF_OK ? 0 : -1;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return -1;    
}

extern "C" int amf_set_framerate(void *e, int32_t framerate)
{
    try
    {
        Encoder *enc = (Encoder*)e;
        AMF_RESULT res = AMF_FAIL;
        AMFRate rate = ::AMFConstructRate(framerate, 1);
        switch (enc->m_dataFormat)
        {
        case H264:
            res = enc->m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, rate);
            break;
        case H265:
            res = enc->m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, rate);
            break;
        }
        return res == AMF_OK ? 0 : -1;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return -1;    
}