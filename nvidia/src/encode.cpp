#include <fstream>
#include <iostream>
#include <memory>
#include <cuda.h>
#include <Samples/Utils/NvCodecUtils.h>
#include <Samples/NvCodec/NvEncoder/NvEncoderCuda.h>
#include <Samples/Utils/Logger.h>
#include <Samples/Utils/NvEncoderCLIOptions.h>

#include "common.h"
#include "callback.h"

simplelogger::Logger *logger = simplelogger::LoggerFactory::CreateConsoleLogger();
struct Encoder
{
    int32_t width;
    int32_t height;
    NV_ENC_BUFFER_FORMAT format;
    int gpu;
    NvEncoderInitParam initParam;
    NvEncoderCuda *pEnc = NULL;
    CUcontext cuContext = NULL;

    Encoder(int32_t width, int32_t height, NV_ENC_BUFFER_FORMAT format, int32_t gpu):
        width(width), height(height), format(format), gpu(gpu) {}
};

extern "C" int nvidia_destroy_encoder(void *ve)
{
    Encoder *e = (Encoder*)ve;
    if (e)
    {
        if (e->pEnc)
        {
            e->pEnc->DestroyEncoder();
            delete e->pEnc;
        }
        if (e->cuContext)
        {
            cuCtxDestroy(e->cuContext);
        }
    }
    return 0;
}

extern "C" void* nvidia_new_encoder(int32_t width, int32_t height, CodecID codecID, PixelFormat nformat, int32_t gpu)
{
    Encoder * e = NULL;
    try 
    {
        if (nformat != NV12)
        {
            goto _exit;
        }
        NV_ENC_BUFFER_FORMAT format = NV_ENC_BUFFER_FORMAT_NV12;
        if (codecID != H264 && codecID != HEVC)
        {
            goto _exit;
        }
        GUID guidCodec = NV_ENC_CODEC_H264_GUID;
        if (HEVC == codecID)
        {
            guidCodec = NV_ENC_CODEC_HEVC_GUID;
        }

        e = new Encoder(width, height, format, gpu);
        if (!e)
        {
            std::cout << "failed to new Encoder" << std::endl;
            goto _exit;
        }
        NvEncoderInitParam initParam;
        if(!ck(cuInit(0)))
        {
            goto _exit;
        }
        int nGpu = 0;
        if (!ck(cuDeviceGetCount(&nGpu)))
        {
            goto _exit;
        }
        if (gpu < 0 || gpu >= nGpu)
        {
            std::cout << "GPU ordinal out of range. Should be within [" << 0 << ", " << nGpu - 1 << "]" << std::endl;
            goto _exit;
        }
        CUdevice cuDevice = 0;
        if (!ck(cuDeviceGet(&cuDevice, gpu)))
        {
            goto _exit;
        }
        char szDeviceName[80];
        if (!ck(cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cuDevice)))
        {
            goto _exit;
        }
        std::cout << "GPU in use: " << szDeviceName << std::endl;
        if (!ck(cuCtxCreate(&e->cuContext, 0, cuDevice)))
        {
            goto _exit;
        }

        e->pEnc = new NvEncoderCuda(e->cuContext, e->width, e->height, e->format, 0); // no delay
        NV_ENC_INITIALIZE_PARAMS initializeParams = { NV_ENC_INITIALIZE_PARAMS_VER };
        NV_ENC_CONFIG encodeConfig = { NV_ENC_CONFIG_VER };

        initializeParams.encodeConfig = &encodeConfig;
        e->pEnc->CreateDefaultEncoderParams(&initializeParams, NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_P3_GUID, NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY);

        // no delay
        initializeParams.encodeConfig->frameIntervalP = 1;
        initializeParams.encodeConfig->rcParams.lookaheadDepth = 0;

        e->pEnc->CreateEncoder(&initializeParams);
    }
    catch (const std::exception &ex)
    {
        std::cout << ex.what();
        goto _exit;
    }
    
    return e;
_exit:
    if (e)
    {
        nvidia_destroy_encoder(e);
        delete e;
    }
    return NULL;
}

// to-do: datas, linesizes
extern "C" int nvidia_encode(void *ve,  uint8_t *data, int32_t len, EncodeCallback callback, void* obj)
{
    Encoder *e = (Encoder*)ve;
    NvEncoderCuda *pEnc = e->pEnc;
    CUcontext cuContext = e->cuContext;
    int ret = -1;

    if (len == pEnc->GetFrameSize())
    {
        std::vector<std::vector<uint8_t>> vPacket;
        const NvEncInputFrame* encoderInputFrame = pEnc->GetNextInputFrame();
        NvEncoderCuda::CopyToDeviceFrame(cuContext, data, 0, (CUdeviceptr)encoderInputFrame->inputPtr,
            (int)encoderInputFrame->pitch,
            pEnc->GetEncodeWidth(),
            pEnc->GetEncodeHeight(),
            CU_MEMORYTYPE_HOST, 
            encoderInputFrame->bufferFormat,
            encoderInputFrame->chromaOffsets,
            encoderInputFrame->numChromaPlanes);
        pEnc->EncodeFrame(vPacket);
        for (std::vector<uint8_t> &packet : vPacket)
        {
            callback(packet.data(), packet.size(), 0, 0, obj);
            ret = 0;
        }
    }
    return ret;
}
