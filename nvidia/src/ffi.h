#ifndef AMF_FFI_H
#define AMF_FFI_H

#include <stdint.h>

/**
 * Input buffer formats
 */
typedef enum _NV_ENC_BUFFER_FORMAT
{
    NV_ENC_BUFFER_FORMAT_UNDEFINED                       = 0x00000000,  /**< Undefined buffer format */
                                                                       
    NV_ENC_BUFFER_FORMAT_NV12                            = 0x00000001,  /**< Semi-Planar YUV [Y plane followed by interleaved UV plane] */
    NV_ENC_BUFFER_FORMAT_YV12                            = 0x00000010,  /**< Planar YUV [Y plane followed by V and U planes] */
    NV_ENC_BUFFER_FORMAT_IYUV                            = 0x00000100,  /**< Planar YUV [Y plane followed by U and V planes] */
    NV_ENC_BUFFER_FORMAT_YUV444                          = 0x00001000,  /**< Planar YUV [Y plane followed by U and V planes] */
    NV_ENC_BUFFER_FORMAT_YUV420_10BIT                    = 0x00010000,  /**< 10 bit Semi-Planar YUV [Y plane followed by interleaved UV plane]. Each pixel of size 2 bytes. Most Significant 10 bits contain pixel data. */
    NV_ENC_BUFFER_FORMAT_YUV444_10BIT                    = 0x00100000,  /**< 10 bit Planar YUV444 [Y plane followed by U and V planes]. Each pixel of size 2 bytes. Most Significant 10 bits contain pixel data.  */
    NV_ENC_BUFFER_FORMAT_ARGB                            = 0x01000000,  /**< 8 bit Packed A8R8G8B8. This is a word-ordered format
                                                                             where a pixel is represented by a 32-bit word with B
                                                                             in the lowest 8 bits, G in the next 8 bits, R in the
                                                                             8 bits after that and A in the highest 8 bits. */
    NV_ENC_BUFFER_FORMAT_ARGB10                          = 0x02000000,  /**< 10 bit Packed A2R10G10B10. This is a word-ordered format
                                                                             where a pixel is represented by a 32-bit word with B
                                                                             in the lowest 10 bits, G in the next 10 bits, R in the
                                                                             10 bits after that and A in the highest 2 bits. */
    NV_ENC_BUFFER_FORMAT_AYUV                            = 0x04000000,  /**< 8 bit Packed A8Y8U8V8. This is a word-ordered format
                                                                             where a pixel is represented by a 32-bit word with V
                                                                             in the lowest 8 bits, U in the next 8 bits, Y in the
                                                                             8 bits after that and A in the highest 8 bits. */
    NV_ENC_BUFFER_FORMAT_ABGR                            = 0x10000000,  /**< 8 bit Packed A8B8G8R8. This is a word-ordered format
                                                                             where a pixel is represented by a 32-bit word with R
                                                                             in the lowest 8 bits, G in the next 8 bits, B in the
                                                                             8 bits after that and A in the highest 8 bits. */
    NV_ENC_BUFFER_FORMAT_ABGR10                          = 0x20000000,  /**< 10 bit Packed A2B10G10R10. This is a word-ordered format
                                                                             where a pixel is represented by a 32-bit word with R
                                                                             in the lowest 10 bits, G in the next 10 bits, B in the
                                                                             10 bits after that and A in the highest 2 bits. */
    NV_ENC_BUFFER_FORMAT_U8                              = 0x40000000,  /**< Buffer format representing one-dimensional buffer. 
                                                                             This format should be used only when registering the 
                                                                             resource as output buffer, which will be used to write
                                                                             the encoded bit stream or H.264 ME only mode output. */
} NV_ENC_BUFFER_FORMAT;

typedef void (*EncodeCallback)(const uint8_t *data,
                                int32_t len,
                                int64_t pts,    
                                int32_t key,
                                const void *obj);

typedef void (*DecodeCallback)(uint8_t *datas[8],
                                int32_t linesizes[8],
                                int32_t surfaceFormat,
                                int32_t width,
                                int32_t height,
                                const void *obj, 
                                int32_t key);

void* nvidia_new_encoder(int32_t width, int32_t height, int codecID, NV_ENC_BUFFER_FORMAT eFormat, int32_t gpu);

int nvidia_encode(void *e,  uint8_t *data, int32_t len, EncodeCallback callback, void* obj);

int nvidia_destroy_encoder(void *e);




void* nvidia_new_decoder(int avCodecID, int iGpu);

int nvidia_decode(void* decoder, uint8_t *data, int len, DecodeCallback callback, void* obj);

int nvidia_destroy_decoder(void* decoder);



#endif  // AMF_FFI_H