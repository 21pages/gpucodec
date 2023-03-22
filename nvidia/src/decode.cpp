#include <iostream>
#include <algorithm>
#include <thread>
#include <Samples/NvCodec/NvDecoder/NvDecoder.h>
#include <Samples/Utils/NvCodecUtils.h>

#include "common.h"
#include "callback.h"

static bool codecID_to_cuCodecID(CodecID codecID, cudaVideoCodec &cuda)
{
    switch (codecID)
    {
    case H264:
        cuda = cudaVideoCodec_H264;
        break;
    case HEVC:
        cuda = cudaVideoCodec_HEVC;
        break;
    case AV1:
        cuda = cudaVideoCodec_AV1;
        break;
    default:
        return false;
    }
    return true;
}

struct Decoder
{
    CudaFunctions *cudl = NULL;
    CuvidFunctions *cvdl = NULL;
    NvDecoder *dec = NULL;
    CUcontext cuContext = NULL;

    Decoder()
    {
        if (cuda_load_functions(&cudl, NULL) < 0)
        {
            NVDEC_THROW_ERROR("cuda_load_functions failed", CUDA_ERROR_UNKNOWN);
        }
        if (cuvid_load_functions(&cvdl, NULL) < 0)
        {
            NVDEC_THROW_ERROR("cuvid_load_functions failed", CUDA_ERROR_UNKNOWN);
        }
    }
};

extern "C" int nvidia_destroy_decoder(void* decoder)
{
    try
    {
        Decoder *p = (Decoder*)decoder;
        if (p)
        {
            if (p->dec)
            {
                delete p->dec;
            }
            if (p->cuContext)
            {
                p->cudl->cuCtxDestroy(p->cuContext);
            }
            if (p->cvdl)
            {
                cuvid_free_functions(&p->cvdl);
            }
            if (p->cudl)
            {
                cuda_free_functions(&p->cudl);
            }
        }
        return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return -1;
}

extern "C" void* nvidia_new_decoder(HWDeviceType device, PixelFormat format, CodecID codecID, int32_t iGpu) 
{
    Decoder *p = NULL;
    try
    {
        p = new Decoder();
        if (!p)
        {
            goto _exit;
        }
        Rect cropRect = {};
        Dim resizeDim = {};
        unsigned int opPoint = 0;
        bool bDispAllLayers = false;
        if (!ck(p->cudl->cuInit(0)))
        {
            goto _exit;
        }
        int nGpu = 0;
        ck(p->cudl->cuDeviceGetCount(&nGpu));
        if (iGpu < 0 || iGpu >= nGpu) {
            std::cout << "GPU ordinal out of range. Should be within [" << 0 << ", " << nGpu - 1 << "]" << std::endl;
            goto _exit;
        }

        CUdevice cuDevice = 0;
        if (!ck(p->cudl->cuDeviceGet(&cuDevice, iGpu)))
        {
            goto _exit;
        }
        char szDeviceName[80];
        if (!ck(p->cudl->cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cuDevice)))
        {
            goto _exit;
        }
        std::cout << "GPU in use: " << szDeviceName << std::endl;
        if (!ck(p->cudl->cuCtxCreate(&p->cuContext, 0, cuDevice)))
        {
            goto _exit;
        }

        cudaVideoCodec cudaCodecID;
        if (!codecID_to_cuCodecID(codecID, cudaCodecID))
        {
            goto _exit;
        }
        bool bLowLatency = true;
        p->dec = new NvDecoder(p->cudl, p->cvdl, p->cuContext, false, cudaCodecID, bLowLatency, false, &cropRect, &resizeDim);
        /* Set operating point for AV1 SVC. It has no impact for other profiles or codecs
        * PFNVIDOPPOINTCALLBACK Callback from video parser will pick operating point set to NvDecoder  */
        p->dec->SetOperatingPoint(opPoint, bDispAllLayers);
        return p;
    }
    catch (const std::exception& ex)
    {
        std::cout << ex.what();
        goto _exit;
    }

_exit:
    if (p)
    {
        nvidia_destroy_decoder(p);
        delete p;
    }
    return NULL;
}

extern "C" int nvidia_decode(void* decoder, uint8_t *data, int len, DecodeCallback callback, void* obj)
{
    try
    {
        Decoder *p = (Decoder*)decoder;
        NvDecoder *dec = p->dec;

        int nFrameReturned = dec->Decode(data, len, CUVID_PKT_ENDOFPICTURE);
        cudaVideoSurfaceFormat format = dec->GetOutputFormat();
        if (format != cudaVideoSurfaceFormat_NV12) {
            return -1;
        }
        bool decoded = false;
        for (int i = 0; i < nFrameReturned; i++) {
            uint8_t *pFrame = dec->GetFrame();
            uint8_t* datas[MAX_DATA_NUM] = {0};
            int32_t linesizes[MAX_DATA_NUM] = {0};
            if (dec->GetWidth() == dec->GetDecodeWidth())
            {
                datas[0] = pFrame;
                datas[1] = datas[0] + dec->GetWidth() * dec->GetHeight();
                linesizes[0] = dec->GetWidth();
                linesizes[1] = linesizes[0];
                callback(datas, linesizes, NV12, dec->GetDecodeWidth(), dec->GetHeight(), obj, 0);
                decoded = true;
            }
            // todo: odd width
        }
        return decoded ? 0 : -1;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return -1;
}
