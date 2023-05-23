#define FFNV_LOG_FUNC
#define FFNV_DEBUG_LOG_FUNC

#include <iostream>
#include <algorithm>
#include <thread>
#include <Samples/NvCodec/NvDecoder/NvDecoder.h>
#include <Samples/Utils/NvCodecUtils.h>

#include "common.h"
#include "callback.h"
#include "system.h"

static void load_driver(CudaFunctions **pp_cudl, CuvidFunctions **pp_cvdl)
{
    if (cuda_load_functions(pp_cudl, NULL) < 0)
    {
        NVDEC_THROW_ERROR("cuda_load_functions failed", CUDA_ERROR_UNKNOWN);
    }
    if (cuvid_load_functions(pp_cvdl, NULL) < 0)
    {
        NVDEC_THROW_ERROR("cuvid_load_functions failed", CUDA_ERROR_UNKNOWN);
    }
}

static void free_driver(CudaFunctions **pp_cudl, CuvidFunctions **pp_cvdl)
{
    if (*pp_cvdl)
    {
        cuvid_free_functions(pp_cvdl);
        *pp_cvdl = NULL;
    }
    if (*pp_cudl)
    {
        cuda_free_functions(pp_cudl);
        *pp_cudl = NULL;
    }
}

extern "C" int nvidia_decode_driver_support()
{
    try
    {
        CudaFunctions *cudl = NULL;
        CuvidFunctions *cvdl = NULL;
        load_driver(&cudl, &cvdl);
        free_driver(&cudl, &cvdl);
        return 0;
    }
    catch(const std::exception& e)
    {
    }
    return -1;
}


class Decoder
{
public:
    CudaFunctions *cudl = NULL;
    CuvidFunctions *cvdl = NULL;
    NvDecoder *dec = NULL;
    CUcontext cuContext = NULL;
    CUgraphicsResource cuResource = NULL;
    ComPtr<ID3D11Texture2D> d3d11_texture = NULL;
    std::unique_ptr<NativeDevice> nativeDevice_ = nullptr;

    Decoder(int)
    {
        load_driver(&cudl, &cvdl);
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
            if (p->cuResource) {
                p->cudl->cuCtxPushCurrent(p->cuContext);
                p->cudl->cuGraphicsUnregisterResource(p->cuResource);
                p->cudl->cuCtxPopCurrent(NULL);
            }
            if (p->cuContext)
            {
                p->cudl->cuCtxDestroy(p->cuContext);
            }
            free_driver(&p->cudl, &p->cvdl);
        }
        return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return -1;
}

static bool dataFormat_to_cuCodecID(DataFormat dataFormat, cudaVideoCodec &cuda)
{
    switch (dataFormat)
    {
    case H264:
        cuda = cudaVideoCodec_H264;
        break;
    case H265:
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

extern "C" void* nvidia_new_decoder(void *opaque, API api, DataFormat dataFormat, SurfaceFormat outputSurfaceFormat) 
{
    Decoder *p = NULL;
    try
    {
        (void)api;
        p = new Decoder(0);
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

        CUdevice cuDevice = 0;
#ifdef _WIN32
        p->nativeDevice_ = std::make_unique<NativeDevice>();
        if (!p->nativeDevice_->Init(ADAPTER_VENDOR_NVIDIA, (ID3D11Device*)opaque)) goto _exit;
        if(!ck(p->cudl->cuD3D11GetDevice(&cuDevice, p->nativeDevice_->adapter_.Get()))) goto _exit;
#else
        int nGpu = 0, gpu = 0;
        ck(p->cudl->cuDeviceGetCount(&nGpu));
        if (gpu >= nGpu) {
            std::cout << "GPU ordinal out of range. Should be within [" << 0 << ", " << nGpu - 1 << "]" << std::endl;
            goto _exit;
        }
        if (!ck(p->cudl->cuDeviceGet(&cuDevice, gpu)))
        {
            goto _exit;
        }
#endif
        char szDeviceName[80];
        if (!ck(p->cudl->cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cuDevice)))
        {
            goto _exit;
        }

        if (!ck(p->cudl->cuCtxCreate(&p->cuContext, 0, cuDevice)))
        {
            goto _exit;
        }

        cudaVideoCodec cudaCodecID;
        if (!dataFormat_to_cuCodecID(dataFormat, cudaCodecID))
        {
            goto _exit;
        }
        bool bUseDeviceFrame = true;
        bool bLowLatency = true;
        p->dec = new NvDecoder(p->cudl, p->cvdl, p->cuContext, bUseDeviceFrame, cudaCodecID, bLowLatency, false, &cropRect, &resizeDim);
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

// static bool CopyDeviceFrame(Decoder *p, unsigned char *dpNv12, int nPitch) {
//     NvDecoder *dec = p->dec;
//     int width = dec->GetWidth();
//     int height = dec->GetHeight();

//     if(!ck(p->cudl->cuCtxPushCurrent(p->cuContext))) return false;
//     ck(p->cudl->cuGraphicsMapResources(1, &p->cuResource, 0));
//     CUarray dstArray;
//     ck(p->cudl->cuGraphicsSubResourceGetMappedArray(&dstArray, p->cuResource, 0, 0));

//     CUDA_MEMCPY2D m = { 0 };
//     m.srcMemoryType = CU_MEMORYTYPE_DEVICE;
//     m.srcDevice = (CUdeviceptr)dpNv12;
//     m.srcPitch = nPitch;
//     m.dstMemoryType = CU_MEMORYTYPE_ARRAY;
//     m.dstArray = dstArray;
//     m.WidthInBytes = width * 4;
//     m.Height = height;
//     ck(p->cudl->cuMemcpy2D(&m));

//     ck(p->cudl->cuGraphicsUnmapResources(1, &p->cuResource, 0));
//     if(!ck(p->cudl->cuCtxPopCurrent(NULL))) return false;
// }

// #include "ColorSpace.h"

static bool create_register_texture(Decoder *p)
{
    if (p->d3d11_texture) return true;
    D3D11_TEXTURE2D_DESC desc;
    NvDecoder *dec = p->dec;
    int width = dec->GetWidth();
    int height = dec->GetHeight();

    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 0;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.MiscFlags = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    HRB(p->nativeDevice_->device_->CreateTexture2D(&desc, nullptr, p->d3d11_texture.ReleaseAndGetAddressOf()));
    if(!ck(p->cudl->cuCtxPushCurrent(p->cuContext))) return false;
    if(!ck(p->cudl->cuGraphicsD3D11RegisterResource(&p->cuResource, p->d3d11_texture.Get(), CU_GRAPHICS_REGISTER_FLAGS_NONE))) return false;
    if(!ck(p->cudl->cuGraphicsResourceSetMapFlags(p->cuResource, CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD))) return false;
    if(!ck(p->cudl->cuCtxPopCurrent(NULL))) return false;
    return true;

}

extern "C" int nvidia_decode(void* decoder, uint8_t *data, int len, DecodeCallback callback, void* obj)
{
    try
    {
        Decoder *p = (Decoder*)decoder;
        NvDecoder *dec = p->dec;

        int nFrameReturned = dec->Decode(data, len, CUVID_PKT_ENDOFPICTURE);
        cudaVideoSurfaceFormat format = dec->GetOutputFormat();
        int width = dec->GetWidth();
        int height = dec->GetHeight();
        bool decoded = false;
        for (int i = 0; i < nFrameReturned; i++) {
            uint8_t *pFrame = dec->GetFrame();
            if (!p->d3d11_texture) {
                if (!create_register_texture(p)) {
                    return -1;
                }
                // if (!CopyDeviceFrame())

            }
            // Nv12ToColor32<BGRA32>(pFrame, dec.GetWidth(), (uint8_t *)dpFrame, 4 * nRGBWidth, dec.GetWidth(), dec.GetHeight(), iMatrix);
            decoded = true;
        }
        return decoded ? 0 : -1;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    return -1;
}
