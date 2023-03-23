#include <stdio.h>
#ifdef _WIN32
#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#endif
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

#include "common.h"
#include "callback.h"

#define AMF_FACILITY        L"AMFEncoder"
#define MILLISEC_TIME       10000

/** Encoder input frame */
struct encoder_frame {
	/** Data for the frame/audio */
	uint8_t *data[MAX_DATA_NUM];

	/** size of each plane */
	uint32_t linesize[MAX_DATA_NUM];

	/** Presentation timestamp */
	int64_t pts;
};

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
private:
    // AMF Internals
    AMFFactoryHelper m_AMFFactory;
    amf::AMFContextPtr m_AMFContext = NULL;
    amf::AMFComputePtr m_AMFCompute = NULL;
    amf::AMFComponentPtr m_AMFEncoder = NULL;
    amf::AMF_MEMORY_TYPE    m_AMFMemoryType;
    amf::AMF_SURFACE_FORMAT m_AMFSurfaceFormat;
    std::pair<int32_t, int32_t> m_Resolution;
    amf_wstring m_codec;
    bool m_OpenCLSubmission = false; // Possible memory leak
    // const
    AMF_COLOR_BIT_DEPTH_ENUM m_eDepth = AMF_COLOR_BIT_DEPTH_8;
    int m_query_timeout = 50;
    int m_frameRate = 30;
    amf_int64 m_bitRateIn = 5000000L;

    // Buffers
	std::vector<uint8_t> m_PacketDataBuffer;

public:
    Encoder(amf::AMF_MEMORY_TYPE memoryType, amf::AMF_SURFACE_FORMAT surfaceFormat, amf_wstring codec, int32_t width, int32_t height):
        m_AMFMemoryType(memoryType),
        m_AMFSurfaceFormat(surfaceFormat), 
        m_codec(codec),
        m_Resolution(width, height) 
    {
        init_result = initialize();
    }

    AMF_RESULT encode(struct encoder_frame* frame, EncodeCallback callback, void* obj)
    {
        amf::AMFSurfacePtr surface = NULL;
        amf::AMFComputeSyncPointPtr pSyncPoint = NULL;
        AMF_RESULT res;
        bool encoded = false;

        // alloc surface
        res = m_AMFContext->AllocSurface(m_AMFMemoryType, m_AMFSurfaceFormat, m_Resolution.first,
										 m_Resolution.second, &surface);
        if (m_OpenCLSubmission)
        {
            res = surface->Convert(amf::AMF_MEMORY_OPENCL);
            AMF_RETURN_IF_FAILED(res, L"Convert() failed");
        }
        AMF_RETURN_IF_FAILED(res, L"AllocSurface() failed");

        if (m_AMFCompute != NULL)
        {
            m_AMFCompute->PutSyncPoint(&pSyncPoint);
        }

        // copy data
        size_t planeCount = surface->GetPlanesCount();
        for (uint8_t i = 0; i < planeCount; i++) {
            amf::AMFPlanePtr plane  = surface->GetPlaneAt(i);
            int32_t          width  = plane->GetWidth();
            int32_t          height = plane->GetHeight();
            int32_t          hpitch = plane->GetHPitch();
            if (m_AMFCompute == NULL)
            {
                void* plane_nat = plane->GetNative();
                for (int32_t py = 0; py < height; py++) {
                    int32_t plane_off = py * hpitch;
                    int32_t frame_off = py * frame->linesize[i];
                    std::memcpy(static_cast<void*>(static_cast<uint8_t*>(plane_nat) + plane_off),
                                static_cast<void*>(frame->data[i] + frame_off), frame->linesize[i]);
                }
            }
            else
            {
                static const amf_size l_origin[] = {0, 0, 0};
                const amf_size        l_size[]   = {(amf_size)width, (amf_size)height, 1};
                res = m_AMFCompute->CopyPlaneFromHost(frame->data[i], l_origin, l_size, frame->linesize[i],
                                                    plane, false);
            }
        }

        if (m_AMFCompute != NULL)
        {
		    res = m_AMFCompute->FinishQueue();
            pSyncPoint->Wait();
		}
        if (m_OpenCLSubmission)
        {
            res = surface->Convert(m_AMFMemoryType);
            AMF_RETURN_IF_FAILED(res, L"Convert() failed");
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
                amf::AMFBufferPtr pBuffer   = amf::AMFBufferPtr(data);
                struct encoder_packet packet;
                packet.size = pBuffer->GetSize();
                if (m_PacketDataBuffer.size() != packet.size) {
                    size_t newBufferSize = (size_t)exp2(ceil(log2((double)packet.size)));
                    m_PacketDataBuffer.resize(packet.size);
                }
                packet.data = m_PacketDataBuffer.data();
                std::memcpy(packet.data, pBuffer->GetNative(), packet.size);
                callback(packet.data, packet.size, 0, 0, obj);
                encoded = true;
                pBuffer = NULL;
        }
        data = NULL;
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
        // to-do: Got Possible memory leak detected when use OpenCLSubmission
        if (m_AMFContext)
        {
            m_AMFContext->Terminate();
            m_AMFContext = NULL; // m_AMFContext is the last
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

        // m_AMFContext
        res = m_AMFFactory.GetFactory()->CreateContext(&m_AMFContext);
        AMF_RETURN_IF_FAILED(res, L"CreateContext() failed");

        // to-do
        switch (m_AMFMemoryType)
        {
        #ifdef _WIN32
        case amf::AMF_MEMORY_DX9:
            res = m_AMFContext->InitDX9(NULL); // can be DX9 or DX9Ex device
            AMF_RETURN_IF_FAILED(res, L"InitDX9(NULL) failed");
            m_OpenCLSubmission = true;
            break;
        case amf::AMF_MEMORY_DX11:
            res = m_AMFContext->InitDX11(NULL); // can be DX11 device
            AMF_RETURN_IF_FAILED(res, L"InitDX11(NULL) failed");
            // m_OpenCLSubmission = true;
            break;
        #endif
        case amf::AMF_MEMORY_VULKAN:
            res = amf::AMFContext1Ptr(m_AMFContext)->InitVulkan(NULL);
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
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, %d) failed", m_frameRate, 1);
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
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, %d) failed", m_frameRate, 1);
        }
        else if (codecStr == amf_wstring(AMFVideoEncoder_AV1))
        {
            // ------------- Encoder params usage---------------
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_LOW_LATENCY);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_USAGE, AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING)");

            // ------------- Encoder params static---------------
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMESIZE, ::AMFConstructSize(m_Resolution.first, m_Resolution.second));
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMESIZE, %dx%d) failed", m_Resolution.first, m_Resolution.second);
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE, AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_LOWEST_LATENCY);
            AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE, AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_LOWEST_LATENCY) failed");
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET, AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET, AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED)");
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_COLOR_BIT_DEPTH, m_eDepth);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_COLOR_BIT_DEPTH, %d) failed", m_eDepth);

            // ------------- Encoder params dynamic ---------------
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_QUERY_TIMEOUT, m_query_timeout); //ms
            AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_QUERY_TIMEOUT, %d) failed", m_query_timeout);
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_TARGET_BITRATE, m_bitRateIn);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_TARGET_BITRATE, %" LPRId64 L") failed", m_bitRateIn);
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMERATE, ::AMFConstructRate(m_frameRate, 1));
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMERATE, %d) failed", m_frameRate, 1);
        }   
        return AMF_OK;
    }

};

static bool convert_codec(CodecID lhs, amf_wstring& rhs)
{
    switch (lhs)
    {
    case H264:
        rhs = AMFVideoEncoderVCE_AVC;
        break;
    case HEVC:
        rhs = AMFVideoEncoder_HEVC;
        break;
    case AV1:
        rhs = AMFVideoEncoder_AV1;
        break;
    default:
        std::cerr << "unsupported codec: " << lhs << "\n";
        return false;
    }
    return true;
}

#include "common.cpp"

extern "C" void* amf_new_encoder(HWDeviceType device, PixelFormat format, CodecID codecID,
                                int32_t width, int32_t height, int32_t gpu) 
{
    try 
    {
        amf_wstring codecStr;
        if (!convert_codec(codecID, codecStr))
        {
            return NULL;
        }
        amf::AMF_MEMORY_TYPE memoryType;
        if (!convert_device(device, memoryType))
        {
            return NULL;
        }
        amf::AMF_SURFACE_FORMAT surfaceFormat;
        if (!convert_format(format, surfaceFormat))
        {
            return NULL;
        }
        Encoder *enc = new Encoder(memoryType, surfaceFormat, codecStr, width, height);
        if (enc && enc->init_result != AMF_OK) {
            enc->destroy();
            delete enc;
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

extern "C" int amf_encode(void *e, uint8_t *data[MAX_DATA_NUM], int32_t linesize[MAX_DATA_NUM], EncodeCallback callback, void* obj)
{
    try
    {
        Encoder *enc = (Encoder*)e;
        struct encoder_frame frame;
        for (int i = 0; i < MAX_DATA_NUM; i++) {
            frame.data[i] = data[i];
            frame.linesize[i] = linesize[i];
        }
        return enc->encode(&frame, callback, obj);
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