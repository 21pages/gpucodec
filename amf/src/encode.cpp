#include <stdio.h>
#ifdef _WIN32
#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#endif
#include "public/common/AMFFactory.h"
#include "public/common/Thread.h"
#include "public/common/AMFSTL.h"
#include "public/common/TraceAdapter.h"
#include "public/include/core/Platform.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"
#include "public/include/components/VideoEncoderAV1.h"
#include "public/samples/CPPSamples/common/EncoderParamsAVC.h"
#include "public/samples/CPPSamples/common/EncoderParamsHEVC.h"
#include "public/samples/CPPSamples/common/EncoderParamsAV1.h"
#include "public/samples/CPPSamples/common/ParametersStorage.h"
#include "public/samples/CPPSamples/common/CmdLineParser.h"
#include "public/samples/CPPSamples/common/CmdLogger.h"
#include "public/samples/CPPSamples/common/PipelineDefines.h"


#define AMF_FACILITY L"EncoderLatency"

#include <fstream>
#include <iostream>
#include <math.h>
#include <cstring>

static bool bMaximumSpeed = true;
static float fFrameRate = 30.f;
static bool bRealTime = false;

#define MILLISEC_TIME     10000

static const wchar_t*  PARAM_NAME_VCN_INSTANCE  = L"VCNINSTANCE";
static const wchar_t*  PARAM_NAME_REALTIME      = L"REALTIME";

#define MAX_AV_PLANES 8

/** Encoder input frame */
struct encoder_frame {
	/** Data for the frame/audio */
	uint8_t *data[MAX_AV_PLANES];

	/** size of each plane */
	uint32_t linesize[MAX_AV_PLANES];

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


class MyEncoder {

private:
    // AMF Internals
    AMFFactoryHelper m_AMFFactory;
    amf::AMFContextPtr m_AMFContext = NULL;
    amf::AMFComputePtr m_AMFCompute = NULL;
    amf::AMFComponentPtr m_AMFEncoder = NULL;
    amf::AMF_SURFACE_FORMAT m_AMFSurfaceFormat;
    amf::AMF_MEMORY_TYPE    m_AMFMemoryType;
    AMF_COLOR_BIT_DEPTH_ENUM eDepth = AMF_COLOR_BIT_DEPTH_8;
    float fFrameRate = 30.f;
    std::pair<uint32_t, uint32_t> m_Resolution;
    amf_wstring codec = AMFVideoEncoderVCE_AVC;
    bool bMaximumSpeed = true;
    bool bRealTime = false;
    bool use_dx11 = true;

    // Buffers
	std::vector<uint8_t> m_PacketDataBuffer;

public:
    MyEncoder(amf::AMF_SURFACE_FORMAT surfaceFormat, uint32_t width, uint32_t height):
        m_AMFSurfaceFormat(surfaceFormat), 
        m_Resolution(width, height) 
    {
        #ifdef _WIN32
        if (use_dx11) {
            m_AMFMemoryType = amf::AMF_MEMORY_DX11;
        } else {
            m_AMFMemoryType = amf::AMF_MEMORY_HOST;
        }
        #else
        m_AMFMemoryType = amf::AMF_MEMORY_VULKAN;
        #endif
        initialize();
    }

    ~MyEncoder() {
        wprintf(L"~MyEncoder()\n");
        destroy();
    }

    amf_pts copy_sum = 0;
    amf_pts query_sum = 0;
    int pts_cnt = 0;

    AMF_RESULT encode(struct encoder_frame* frame, struct encoder_packet *packet)
    {
        amf::AMFSurfacePtr surface = NULL;
        AMF_RESULT res;
        amf_pts start_time = amf_high_precision_clock();

        // alloc surface
        if (use_dx11) {
            res = m_AMFContext->AllocSurface(amf::AMF_MEMORY_DX11, m_AMFSurfaceFormat, m_Resolution.first,
										 m_Resolution.second, &surface);
        } else {
            res = m_AMFContext->AllocSurface(amf::AMF_MEMORY_HOST, m_AMFSurfaceFormat, m_Resolution.first,
										 m_Resolution.second, &surface);
        }
        AMF_RETURN_IF_FAILED(res, L"AllocSurface() failed");
        
        //wprintf(L"AllocSurface:%.4f\n", (double)(amf_high_precision_clock() - start_time) / MILLISEC_TIME);
        start_time = amf_high_precision_clock();

        // copy data
        size_t planeCount = surface->GetPlanesCount();
        for (uint8_t i = 0; i < planeCount; i++) {
            amf::AMFPlanePtr plane  = surface->GetPlaneAt(i);
            int32_t          width  = plane->GetWidth();
            int32_t          height = plane->GetHeight();
            int32_t          hpitch = plane->GetHPitch();
            if (use_dx11) {
                static const amf_size l_origin[] = {0, 0, 0};
                const amf_size        l_size[]   = {(amf_size)width, (amf_size)height, 1};
                res = m_AMFCompute->CopyPlaneFromHost(frame->data[i], l_origin, l_size, frame->linesize[i],
                                                    surface->GetPlaneAt(i), false);
            } else {
                void* plane_nat = plane->GetNative();
                for (int32_t py = 0; py < height; py++) {
                    int32_t plane_off = py * hpitch;
                    int32_t frame_off = py * frame->linesize[i];
                    std::memcpy(static_cast<void*>(static_cast<uint8_t*>(plane_nat) + plane_off),
                                static_cast<void*>(frame->data[i] + frame_off), frame->linesize[i]);
                }
            }
        }
        copy_sum += amf_high_precision_clock() - start_time;

        //wprintf(L"copy:%.4f\n", (double)(amf_high_precision_clock() - start_time) / MILLISEC_TIME);
        // start_time = amf_high_precision_clock();

        //res = surface->Convert(m_AMFMemoryType);

        //wprintf(L"Convert:%.4f\n", (double)(amf_high_precision_clock() - start_time) / MILLISEC_TIME);
        start_time = amf_high_precision_clock();

        // we're doing frame-in/frame-out so the input
        // should never be full
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

        query_sum += amf_high_precision_clock() - start_time;

        //wprintf(L"QueryOutput:%.4f\n", (double)(amf_high_precision_clock() - start_time) / MILLISEC_TIME);
        // start_time = amf_high_precision_clock();

        if (data != NULL) 
        {
            amf::AMFBufferPtr pBuffer   = amf::AMFBufferPtr(data);
            packet->size = pBuffer->GetSize();
            if (m_PacketDataBuffer.size() != packet->size) {
                size_t newBufferSize = (size_t)exp2(ceil(log2((double)packet->size)));
                m_PacketDataBuffer.resize(packet->size);
            }
            packet->data = m_PacketDataBuffer.data();
            std::memcpy(packet->data, pBuffer->GetNative(), packet->size);

            //wprintf(L"memcpy:%.4f\n", (double)(amf_high_precision_clock() - start_time) / MILLISEC_TIME);

            pts_cnt++;
            if (pts_cnt == 100) {
                wprintf(L"use dx11: %d, copy:%.4f, query:%.4f\n", use_dx11, (double)copy_sum / MILLISEC_TIME / pts_cnt, (double)query_sum / MILLISEC_TIME / pts_cnt);
            }

            return AMF_OK;
        }
        return AMF_FAIL;
    }

private:

    AMF_RESULT initialize() 
    {
        ParametersStorage params;
        AMF_RESULT res;
        
        res = ReadParams(&params);
        AMF_RETURN_IF_FAILED(res, L"Command line arguments couldn't be parsed");
        res = ValidateParams(&params);
        AMF_RETURN_IF_FAILED(res, L"ValidateParams");
        res = m_AMFFactory.Init();
        AMF_RETURN_IF_FAILED(res, L"AMF Failed to initialize");
        ::amf_increase_timer_precision();
        amf::AMFTraceEnableWriter(AMF_TRACE_WRITER_CONSOLE, true);
        amf::AMFTraceEnableWriter(AMF_TRACE_WRITER_DEBUG_OUTPUT, true);
        // m_AMFContext
        res = m_AMFFactory.GetFactory()->CreateContext(&m_AMFContext);
        AMF_RETURN_IF_FAILED(res, L"CreateContext() failed");

        if (m_AMFMemoryType == amf::AMF_MEMORY_VULKAN)
        {
            res = amf::AMFContext1Ptr(m_AMFContext)->InitVulkan(NULL);
            AMF_RETURN_IF_FAILED(res, L"InitVulkan(NULL) failed");
            // PrepareFillFromHost(m_AMFContext);
        }
        #ifdef _WIN32
        else if (m_AMFMemoryType == amf::AMF_MEMORY_DX9)
        {
            res = m_AMFContext->InitDX9(NULL); // can be DX9 or DX9Ex device
            AMF_RETURN_IF_FAILED(res, L"InitDX9(NULL) failed");
        }
        else if (m_AMFMemoryType == amf::AMF_MEMORY_DX11)
        {
            res = m_AMFContext->InitDX11(NULL); // can be DX11 device
            AMF_RETURN_IF_FAILED(res, L"InitDX11(NULL) failed");
            // PrepareFillFromHost(m_AMFContext);
        }
        #endif

        if (use_dx11) {
            res = m_AMFContext->GetCompute(amf::AMF_MEMORY_DX11, &m_AMFCompute);
            AMF_RETURN_IF_FAILED(res, L"GetCompute DX11 failed");
        }

        wprintf(L"Encoder: %s\n", codec.c_str());
        // component: encoder
        res = m_AMFFactory.GetFactory()->CreateComponent(m_AMFContext, codec.c_str(), &m_AMFEncoder);
        AMF_RETURN_IF_FAILED(res, L"CreateComponent(%s) failed", codec.c_str());

        res = SetEncoderDefaults(&params, codec);
        AMF_RETURN_IF_FAILED(res, L"Could not set default values in encoder.");

        res = m_AMFEncoder->Init(m_AMFSurfaceFormat, m_Resolution.first, m_Resolution.second);
        AMF_RETURN_IF_FAILED(res, L"encoder->Init() failed");

        return AMF_OK;
    }
   
    AMF_RESULT destroy()
    {
        m_AMFEncoder->Terminate();
        m_AMFEncoder = NULL;
        m_AMFContext->Terminate();
        m_AMFContext = NULL; // m_AMFContext is the last
        m_AMFFactory.Terminate();
    }

    AMF_RESULT RegisterParams(ParametersStorage* pParams)
    {
        pParams->SetParamDescription(PARAM_NAME_ENGINE, ParamCommon, L"Memory type: DX9Ex, DX11, Vulkan (h.264 only)", ParamConverterMemoryType);
        pParams->SetParamDescription(PARAM_NAME_CODEC, ParamCommon, L"Codec name (AVC or H264, HEVC or H265, AV1)", ParamConverterCodec);
        pParams->SetParamDescription(PARAM_NAME_INPUT_FORMAT, ParamCommon, L"Supported file formats: RGBA_F16, R10G10B10A2, NV12, P010", ParamConverterFormat);
        pParams->SetParamDescription(PARAM_NAME_INPUT_FRAMES, ParamCommon, L"Output number of frames", ParamConverterInt64);
        pParams->SetParamDescription(PARAM_NAME_OUTPUT_WIDTH, ParamCommon, L"Output resolution, width", ParamConverterInt64);
        pParams->SetParamDescription(PARAM_NAME_OUTPUT_HEIGHT, ParamCommon, L"Output resolution, height", ParamConverterInt64);
        pParams->SetParamDescription(PARAM_NAME_VCN_INSTANCE, ParamCommon, L"VCN to test (0 or 1). Navi21 and up only", ParamConverterInt64);
        pParams->SetParamDescription(PARAM_NAME_OUTPUT, ParamCommon, L"Output file name", NULL);
        pParams->SetParamDescription(PARAM_NAME_INPUT, ParamCommon, L"Input file name", NULL);
        pParams->SetParamDescription(PARAM_NAME_INPUT_WIDTH, ParamCommon, L"Input file width", ParamConverterInt64);
        pParams->SetParamDescription(PARAM_NAME_INPUT_HEIGHT, ParamCommon, L"Input file height", ParamConverterInt64);
        pParams->SetParamDescription(PARAM_NAME_REALTIME, ParamCommon, L"Bool, Keep real-time framerate, default false", ParamConverterBoolean);

        return AMF_OK;
    }

    AMF_RESULT ReadParams(ParametersStorage* params)
    {
        RegisterParams(params);
        if (codec == amf_wstring(AMFVideoEncoderVCE_AVC))
        {
            RegisterEncoderParamsAVC(params);
        }
        else if (codec == amf_wstring(AMFVideoEncoder_HEVC))
        {
            RegisterEncoderParamsHEVC(params);
        }
        else if (codec == amf_wstring(AMFVideoEncoder_AV1))
        {
            RegisterEncoderParamsAV1(params);
        }
        else
        {
            LOG_ERROR(L"Invalid codec ID");
            return AMF_FAIL;
        }

        // // parse parameters for a final time
        // if (!parseCmdLineParameters(params))
        // {
        //     return AMF_FAIL;
        // }
        return AMF_OK;
    }

    AMF_RESULT ValidateParams(ParametersStorage * pParams)
    {
        amf::AMFVariant tmp;
        if (eDepth == AMF_COLOR_BIT_DEPTH_10 && (m_AMFSurfaceFormat == amf::AMF_SURFACE_NV12 || m_AMFSurfaceFormat == amf::AMF_SURFACE_YV12 || m_AMFSurfaceFormat == amf::AMF_SURFACE_BGRA
            || m_AMFSurfaceFormat == amf::AMF_SURFACE_ARGB || m_AMFSurfaceFormat == amf::AMF_SURFACE_RGBA || m_AMFSurfaceFormat == amf::AMF_SURFACE_GRAY8 || m_AMFSurfaceFormat == amf::AMF_SURFACE_YUV420P
            || m_AMFSurfaceFormat == amf::AMF_SURFACE_U8V8 || m_AMFSurfaceFormat == amf::AMF_SURFACE_YUY2))
        {
            if (pParams->GetParam(PARAM_NAME_INPUT_FORMAT, tmp) == AMF_OK)
            {
                printf("[ERROR] Selected surface format is not a 10-bit format, requested parameters combination can't be applied. Program will terminate\n");
                return AMF_INVALID_ARG;
            }

            printf("[WARNING] Default surface format NV12 is an 8-bit format. Program will use P010 (10-bit) format instead.\n");
            m_AMFSurfaceFormat = amf::AMF_SURFACE_P010;
        }
        else if (eDepth == AMF_COLOR_BIT_DEPTH_8 && (m_AMFSurfaceFormat == amf::AMF_SURFACE_P010 || m_AMFSurfaceFormat == amf::AMF_SURFACE_R10G10B10A2 || m_AMFSurfaceFormat == amf::AMF_SURFACE_RGBA_F16
            || m_AMFSurfaceFormat == amf::AMF_SURFACE_UYVY || m_AMFSurfaceFormat == amf::AMF_SURFACE_Y210 || m_AMFSurfaceFormat == amf::AMF_SURFACE_Y410 || m_AMFSurfaceFormat == amf::AMF_SURFACE_Y416 || m_AMFSurfaceFormat == amf::AMF_SURFACE_GRAY32))
        {
            if (pParams->GetParam(AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH, tmp) == AMF_OK)
            {
                printf("[ERROR] Selected surface format is not a 10-bit format, requested parameters combination can't be applied. Program will terminate\n");
                return AMF_INVALID_ARG;
            }

            printf("[WARNING] Default bit depth is 8, but selected surface format is not an 8-bit format. Color depth will be changed to 10 bits\n");
            eDepth = AMF_COLOR_BIT_DEPTH_10;
        }

        if ( ((pParams->GetParam(PARAM_NAME_INPUT, NULL) == AMF_OK) || (pParams->GetParam(PARAM_NAME_INPUT_WIDTH, NULL) == AMF_OK) || (pParams->GetParam(PARAM_NAME_INPUT_HEIGHT, NULL) == AMF_OK)) &&
            ((pParams->GetParam(PARAM_NAME_OUTPUT_WIDTH, NULL) == AMF_OK) || (pParams->GetParam(PARAM_NAME_OUTPUT_HEIGHT, NULL) == AMF_OK)) )
        {
            printf("[WARNING] Input and output dimensions are exclusive - output values ignored and input values used\n");
        }

        return AMF_OK;
    };

    AMF_RESULT SetEncoderDefaults(ParametersStorage* pParams, const  amf_wstring& codecStr)
    {
        AMF_RESULT res;
        if (codecStr == amf_wstring(AMFVideoEncoderVCE_AVC))
        {
            // AMF_VIDEO_ENCODER_USAGE needs to be set before the rest
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCODING) failed");

            if (bMaximumSpeed)
            {
                m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0);
                // do not check error for AMF_VIDEO_ENCODER_B_PIC_PATTERN - can be not supported - check Capability Manager sample
                res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED);
                AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED) failed");
            }

            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(m_Resolution.first, m_Resolution.second));
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, %dx%d) failed", m_Resolution.first, m_Resolution.second);
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true);
            AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true) failed");

            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_QUERY_TIMEOUT, 50); //ms
            AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_QUERY_TIMEOUT, 50) failed");
        }
        else if (codecStr == amf_wstring(AMFVideoEncoder_HEVC))
        {
            // usage parameters come first
            AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderUsage, m_AMFEncoder));

            // AMF_VIDEO_ENCODER_HEVC_USAGE needs to be set before the rest
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_ULTRA_LOW_LATENCY);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCODING)");

            // initialize command line parameters
            AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderStatic, m_AMFEncoder));
            AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderDynamic, m_AMFEncoder));

            if (bMaximumSpeed)
            {
                res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED);
                AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED)");
            }

            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, ::AMFConstructSize(m_Resolution.first, m_Resolution.second));
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, %dx%d) failed", m_Resolution.first, m_Resolution.second);

            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_LOWLATENCY_MODE, true);
            AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true) failed");

            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUERY_TIMEOUT, 50); //ms
            AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUERY_TIMEOUT, 50) failed");
        }
        else if (codecStr == amf_wstring(AMFVideoEncoder_AV1))
        {
            // usage parameters come first
            AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderUsage, m_AMFEncoder));

            // AMF_VIDEO_ENCODER_AV1_USAGE needs to be set before the rest
            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_USAGE, AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_USAGE, AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING)");

            // initialize command line parameters
            AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderStatic, m_AMFEncoder));
            AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderDynamic, m_AMFEncoder));

            if (bMaximumSpeed)
            {
                res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET, AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED);
                AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET, AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED)");
            }

            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMESIZE, ::AMFConstructSize(m_Resolution.first, m_Resolution.second));
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMESIZE, %dx%d) failed", m_Resolution.first, m_Resolution.second);

            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE, AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_LOWEST_LATENCY);
            AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE, AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_LOWEST_LATENCY) failed");

            res = m_AMFEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_QUERY_TIMEOUT, 50); //ms
            AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_QUERY_TIMEOUT, 50) failed");
        }
        return AMF_OK;
    }

};

typedef void (*EncodeCallback)(const uint8_t *data, int len, int64_t pts, int key,
                               const void *obj);

extern "C" MyEncoder* amf_new_encoder(uint32_t width, uint32_t height, amf::AMF_SURFACE_FORMAT surfaceFormat) 
{
    MyEncoder *enc = new MyEncoder(surfaceFormat, width, height);

    return enc;
}

extern "C" int amf_encode(MyEncoder *enc, uint8_t *data[MAX_AV_PLANES], uint32_t linesize[MAX_AV_PLANES], EncodeCallback callback, void* obj)
{
    struct encoder_frame frame;
    for (int i = 0; i < MAX_AV_PLANES; i++) {
        frame.data[i] = data[i];
        frame.linesize[i] = linesize[i];
    }
    struct encoder_packet packet;
    enc->encode(&frame, &packet);
    callback(packet.data, packet.size, 0, 0, obj);
    return 0;
}

// extern "C" int amf_destroy_encoder(MyEncoder *enc)
// {
//     struct encoder_frame frame;
//     for (int i = 0; i < MAX_AV_PLANES; i++) {
//         frame.data[i] = data[i];
//         frame.linesize[i] = linesize[i];
//     }
//     struct encoder_packet packet;
//     enc->encode(&frame, &packet);
//     return 0;
// }
