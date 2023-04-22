#ifndef AMF_FFI_H
#define AMF_FFI_H

#include "../../hw_common/src/callback.h"

int intel_driver_support();

void* intel_new_encoder(void* pD3dDevice, int32_t dataFormat,
                        int32_t width, int32_t height, 
                        int32_t kbs, int32_t framerate, int32_t gop,
                        int32_t pitchs[MAX_DATA_NUM]);

int intel_encode(void *encoder,  void* tex, EncodeCallback callback, void* obj);

int intel_destroy_encoder(void *encoder);

void* intel_new_decoder(void* hdl, int32_t deviceType, int32_t format,int32_t codecID);

int intel_decode(void* decoder, uint8_t *data, int len, DecodeCallback callback, void* obj);

int intel_destroy_decoder(void* decoder);

int intel_set_bitrate(void *e, int32_t kbs);

int intel_set_framerate(void *e, int32_t framerate);



#endif  // AMF_FFI_H