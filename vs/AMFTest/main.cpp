#include <Windows.h>
#include <callback.h>
#include <common.h>
#include <iostream>
#include <stdint.h>
#include <system.h>
#include <fstream>
#include <vector>

extern "C" {
void *amf_new_decoder(void *device, int64_t luid, int32_t api,
                      int32_t dataFormat, bool outputSharedHandle);
int amf_decode(void *decoder, uint8_t *data, int32_t length,
               DecodeCallback callback, void *obj);
int amf_destroy_decoder(void *decoder);

}

static const uint8_t *encode_data;
static int32_t encode_len;
static void *decode_shared_handle;

extern "C" static void decode_callback(void *shared_handle, const void *obj) {
  decode_shared_handle = shared_handle;
}

extern "C" void log_gpucodec(int level, const char *message) {
  std::cout << message << std::endl;
}


int main() {
    Adapters adapters;
    adapters.Init(ADAPTER_VENDOR_AMD);
    if (adapters.adapters_.size() == 0) {
        std::cout << "no amd adapter" << std::endl;
        return -1;
    }
    int64_t luid = LUID(adapters.adapters_[0].get()->desc1_);
    DataFormat dataFormat = H265;
    void* decoder =
        amf_new_decoder(NULL, luid, API_DX11, dataFormat, false);
    if (!decoder) {
        std::cerr << "create decoder failed" << std::endl;
        return -1;
    }
    std::vector<int> fileLengths = { 64156,726,310,112,86,287,1035,2418,3085,4336,4654,6190,5675 };

    std::ifstream file("test.h265", std::ios::binary);
    if (!file) {
        std::cout << "Failed to open the file." << std::endl;
        return 1;
    }

    std::remove("texture/texture.yuv");

    std::vector<char> buffer; 

    for (int length : fileLengths) {
        buffer.resize(length);
        if (!file.read(buffer.data(), length)) {
            std::cout << "Failed to read data from the file." << std::endl;
            return -1;
        }
        if (0 != amf_decode(decoder, (uint8_t*)buffer.data(), length,
            decode_callback, NULL)) {
            std::cerr << "decode failed" << std::endl;
            return -1;
        }
    }

    std::cout << "decode " << fileLengths.size() << " frames, please open texture/texture.yuv with yuvplayer.";

    file.close();
}