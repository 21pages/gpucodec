#include <Windows.h>
#include <callback.h>
#include <common.h>
#include <iostream>
#include <stdint.h>

extern "C" void *dxgi_new_capturer();
extern "C" void *dxgi_device(void *self);
extern "C" void *dxgi_capture(void *self, int wait_ms);
extern "C" void destroy_dxgi_capturer(void *self);

extern "C" void *amf_new_encoder(void *hdl, int64_t luid, API api,
                                 DataFormat dataFormat, int32_t width,
                                 int32_t height, int32_t kbs, int32_t framerate,
                                 int32_t gop);
extern "C" int amf_encode(void *e, void *tex, EncodeCallback callback,
                          void *obj);
extern "C" int amf_destroy_encoder(void *e);

extern "C" void *amf_new_decoder(int64_t luid, API api, DataFormat dataFormat,
                                 SurfaceFormat outputSurfaceFormat);
extern "C" int amf_decode(void *decoder, uint8_t *data, int32_t length,
                          DecodeCallback callback, void *obj);
extern "C" int amf_destroy_decoder(void *decoder);

extern "C" void *CreateDXGIRender(int64_t luid);
extern "C" int DXGIRenderTexture(void *render, HANDLE shared_handle);
extern "C" void DestroyDXGIRender(void *render);

static const uint8_t *encode_data;
static int32_t encode_len;
static void *decode_shared_handle;

extern "C" static void encode_callback(const uint8_t *data, int32_t len,
                                       int32_t key, const void *obj) {
  encode_data = data;
  encode_len = len;
  std::cerr << "encode len" << len << std::endl;
}

extern "C" static void decode_callback(void *shared_handle, const void *obj) {
  decode_shared_handle = shared_handle;
}

int main() {
  int64_t luid = 63666; // get from texcodec/example/available
  DataFormat dataFormat = H265;
  int width = 2880;
  int height = 1800;

  void *dup = dxgi_new_capturer();
  if (!dup) {
    std::cerr << "create duplicator failed" << std::endl;
    return -1;
  }
  void *device = dxgi_device(dup);
  void *encoder = amf_new_encoder(device, luid, API_DX11, dataFormat, width,
                                  height, 4000, 30, 0xFFFF);
  if (!encoder) {
    std::cerr << "create encoder failed" << std::endl;
    return -1;
  }
  void *decoder =
      amf_new_decoder(luid, API_DX11, dataFormat, SURFACE_FORMAT_BGRA);
  if (!decoder) {
    std::cerr << "create decoder failed" << std::endl;
    return -1;
  }
  void *render = CreateDXGIRender(luid);
  if (!render) {
    std::cerr << "create render failed" << std::endl;
    return -1;
  }

  while (true) {
    void *texture = dxgi_capture(dup, 100);
    if (!texture) {
      std::cerr << "texture is NULL" << std::endl;
      continue;
    }
    if (0 != amf_encode(encoder, texture, encode_callback, NULL)) {
      std::cerr << "encode failed" << std::endl;
      continue;
    }
    if (0 != amf_decode(decoder, (uint8_t *)encode_data, encode_len,
                        decode_callback, NULL)) {
      std::cerr << "decode failed" << std::endl;
      continue;
    }
    if (0 != DXGIRenderTexture(render, decode_shared_handle)) {
      std::cerr << "render failed" << std::endl;
      continue;
    }
  }

  // no release temporarily
}