#ifndef AMF_FFI_H
#define AMF_FFI_H

#include "../../common/src/callback.h"

int amf_driver_support();

void* amf_new_encoder(int32_t device, int32_t format, int32_t codecID, int32_t width, int32_t height, int32_t pitchs[MAX_DATA_NUM]);

int amf_encode(void *e, uint8_t *data[MAX_DATA_NUM], int32_t linesize[MAX_DATA_NUM], EncodeCallback callback, void* obj);

int amf_destroy_encoder(void *enc);

void* amf_new_decoder(int32_t device, int32_t format, int32_t codecID);

int amf_decode(void *decoder, uint8_t *data, int32_t length, DecodeCallback callback, void *obj);

int amf_destroy_decoder(void *decoder);

int amf_set_bitrate(void *e, int32_t bitrate);

int amf_set_framerate(void *e, int32_t framerate);

#endif // AMF_FFI_H