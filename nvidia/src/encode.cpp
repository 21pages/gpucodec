#include <fstream>
#include <iostream>
#include <memory>
#include <dynlink_cuda.h>
#include <dynlink_loader.h>
#include <Samples/Utils/NvCodecUtils.h>
#include <Samples/NvCodec/NvEncoder/NvEncoderCuda.h>
#include <Samples/Utils/Logger.h>
#include <Samples/Utils/NvEncoderCLIOptions.h>

#include "common.h"
#include "callback.h"

simplelogger::Logger *logger = simplelogger::LoggerFactory::CreateConsoleLogger();

static void load_driver(CudaFunctions **pp_cuda_dl, NvencFunctions **pp_nvenc_dl)
{
    if (cuda_load_functions(pp_cuda_dl, NULL) < 0)
    {
        NVENC_THROW_ERROR("cuda_load_functions failed", NV_ENC_ERR_GENERIC);
    }
    if (nvenc_load_functions(pp_nvenc_dl, NULL) < 0)
    {
        NVENC_THROW_ERROR("nvenc_load_functions failed", NV_ENC_ERR_GENERIC);
    }
}

static void free_driver(CudaFunctions **pp_cuda_dl, NvencFunctions **pp_nvenc_dl)
{
    if (*pp_nvenc_dl)
    {
        nvenc_free_functions(pp_nvenc_dl);
        *pp_nvenc_dl = NULL;
    }
    if (*pp_cuda_dl)
    {
        cuda_free_functions(pp_cuda_dl);
        *pp_cuda_dl = NULL;
    }
}

extern "C" int nvidia_encode_driver_support()
{
    try
    {
        CudaFunctions *cuda_dl = NULL;
        NvencFunctions *nvenc_dl = NULL;
        load_driver(&cuda_dl, &nvenc_dl);
        free_driver(&cuda_dl, &nvenc_dl);
        return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return -1;
}

struct Encoder
{
    CudaFunctions *cuda_dl = NULL;
    NvencFunctions *nvenc_dl = NULL;
    int32_t width;
    int32_t height;
    NV_ENC_BUFFER_FORMAT format;
    int gpu;
    NvEncoderInitParam initParam;
    NvEncoderCuda *pEnc = NULL;
    CUcontext cuContext = NULL;

    Encoder(int32_t width, int32_t height, NV_ENC_BUFFER_FORMAT format, int32_t gpu):
        width(width), height(height), format(format), gpu(gpu)
    {
        load_driver(&cuda_dl, &nvenc_dl);
    }
};

extern "C" int nvidia_destroy_encoder(void *encoder)
{
    try
    {
        Encoder *e = (Encoder*)encoder;
        if (e)
        {
            if (e->pEnc)
            {
                e->pEnc->DestroyEncoder();
                delete e->pEnc;
            }
            if (e->cuContext)
            {
                e->cuda_dl->cuCtxDestroy(e->cuContext);
            }
            free_driver(&e->cuda_dl, &e->nvenc_dl);
        }
        return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return -1;
}

extern "C" void* nvidia_new_encoder(HWDeviceType device, PixelFormat nformat,CodecID codecID, int32_t width, int32_t height, int32_t gpu)
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
        if(!ck(e->cuda_dl->cuInit(0)))
        {
            goto _exit;
        }
        int nGpu = 0;
        if (!ck(e->cuda_dl->cuDeviceGetCount(&nGpu)))
        {
            goto _exit;
        }
        if (gpu < 0 || gpu >= nGpu)
        {
            std::cout << "GPU ordinal out of range. Should be within [" << 0 << ", " << nGpu - 1 << "]" << std::endl;
            goto _exit;
        }
        CUdevice cuDevice = 0;
        if (!ck(e->cuda_dl->cuDeviceGet(&cuDevice, gpu)))
        {
            goto _exit;
        }
        char szDeviceName[80];
        if (!ck(e->cuda_dl->cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cuDevice)))
        {
            goto _exit;
        }
        std::cout << "GPU in use: " << szDeviceName << std::endl;
        if (!ck(e->cuda_dl->cuCtxCreate(&e->cuContext, 0, cuDevice)))
        {
            goto _exit;
        }

        e->pEnc = new NvEncoderCuda(e->cuda_dl, e->nvenc_dl, e->cuContext, e->width, e->height, e->format, 0); // no delay
        NV_ENC_INITIALIZE_PARAMS initializeParams = { NV_ENC_INITIALIZE_PARAMS_VER };
        NV_ENC_CONFIG encodeConfig = { NV_ENC_CONFIG_VER };

        initializeParams.encodeConfig = &encodeConfig;
        e->pEnc->CreateDefaultEncoderParams(&initializeParams, NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_P3_GUID, NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY);

        // no delay
        initializeParams.encodeConfig->frameIntervalP = 1;
        initializeParams.encodeConfig->rcParams.lookaheadDepth = 0;

        e->pEnc->CreateEncoder(&initializeParams);
        return e;
    }
    catch (const std::exception &ex)
    {
        std::cout << ex.what();
        goto _exit;
    }
    
_exit:
    if (e)
    {
        nvidia_destroy_encoder(e);
        delete e;
    }
    return NULL;
}

// to-do: datas, linesizes
extern "C" int nvidia_encode(void *encoder,  uint8_t* datas[MAX_DATA_NUM], int32_t linesizes[MAX_DATA_NUM], EncodeCallback callback, void* obj)
{
    try
    {
        Encoder *e = (Encoder*)encoder;
        NvEncoderCuda *pEnc = e->pEnc;
        CUcontext cuContext = e->cuContext;
        bool encoded = false;

        // to-do: linesizes, wrong calculate
        int len = e->height * (linesizes[0] + (linesizes[1] + 1) / 2);
        uint8_t *pData = datas[0];
        if (len == pEnc->GetFrameSize())
        {
            std::vector<std::vector<uint8_t>> vPacket;
            const NvEncInputFrame* encoderInputFrame = pEnc->GetNextInputFrame();
            NvEncoderCuda::CopyToDeviceFrame(e->cuda_dl, cuContext, pData, 0, (CUdeviceptr)encoderInputFrame->inputPtr,
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
                encoded = true;
            }
        }
        return encoded ? 0 : -1;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return -1;
}

// extern "C" int nvidia_encode_linesize(PixelFormat format, int32_t linesizes[MAX_DATA_NUM])
// {

// }