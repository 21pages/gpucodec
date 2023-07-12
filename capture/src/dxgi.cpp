#include <DDA.h>
#include <Windows.h>
#include <chrono>
#include <ctime>
#include <string>

static bool SaveTextureToFile(ID3D11Texture2D *texture,
                              const std::string &filename) {
  if (!texture)
    return false;

  ID3D11Device *device = nullptr;
  texture->GetDevice(&device);
  ID3D11DeviceContext *context = nullptr;
  device->GetImmediateContext(&context);

  D3D11_TEXTURE2D_DESC desc;
  texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ID3D11Texture2D *stagingTexture = nullptr;
  HRESULT hr = device->CreateTexture2D(&desc, nullptr, &stagingTexture);
  if (FAILED(hr)) {
    return false;
  }

  context->CopyResource(stagingTexture, texture);

  D3D11_MAPPED_SUBRESOURCE resource;
  hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &resource);
  if (FAILED(hr)) {
    stagingTexture->Release();
    return false;
  }

  BITMAPFILEHEADER bmpHeader;
  bmpHeader.bfType = 0x4d42;
  bmpHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  bmpHeader.bfSize = bmpHeader.bfOffBits + resource.RowPitch * desc.Height;
  bmpHeader.bfReserved1 = 0;
  bmpHeader.bfReserved2 = 0;

  BITMAPINFOHEADER bmpInfoHeader;
  bmpInfoHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmpInfoHeader.biWidth = desc.Width;
  bmpInfoHeader.biHeight = desc.Height;
  bmpInfoHeader.biPlanes = 1;
  bmpInfoHeader.biBitCount = 32;
  bmpInfoHeader.biCompression = BI_RGB;
  bmpInfoHeader.biSizeImage = 0;
  bmpInfoHeader.biXPelsPerMeter = 0;
  bmpInfoHeader.biYPelsPerMeter = 0;
  bmpInfoHeader.biClrUsed = 0;
  bmpInfoHeader.biClrImportant = 0;

  std::ofstream file(filename, std::ios::out | std::ios::binary);
  if (!file.is_open()) {
    context->Unmap(stagingTexture, 0);
    stagingTexture->Release();
    return false;
  }
  file.write(reinterpret_cast<const char *>(&bmpHeader),
             sizeof(BITMAPFILEHEADER));
  file.write(reinterpret_cast<const char *>(&bmpInfoHeader),
             sizeof(BITMAPINFOHEADER));

  char *pixels = static_cast<char *>(resource.pData);
  for (UINT row = 0; row < desc.Height; ++row) {
    file.write(pixels, desc.Width * 4);
    pixels += resource.RowPitch;
  }

  file.close();

  context->Unmap(stagingTexture, 0);
  stagingTexture->Release();

  return true;
}

extern "C" void *dxgi_new_capturer() {
  DemoApplication *d = new DemoApplication();
  HRESULT hr = d->Init();
  if (FAILED(hr)) {
    delete d;
    d = NULL;
    return NULL;
  }

  return d;
}

extern "C" void *dxgi_device(void *capturer) {
  DemoApplication *d = (DemoApplication *)capturer;
  return d->Device();
}

static void save_bmp(void *texture) {
  const char *dir = "texture_bmp";
  DWORD attrib = GetFileAttributesA(dir);
  if (attrib == INVALID_FILE_ATTRIBUTES ||
      !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
    if (!CreateDirectoryA(dir, NULL)) {
      std::cout << "Failed to create directory: " << dir << std::endl;
      return;
    } else {
      std::cout << "Directory created: " << dir << std::endl;
    }
  } else {
    // already exists
  }
  static int index = 0;
  if (index++ % 1000 == 0) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    std::tm local_tm;
    localtime_s(&local_tm, &time_t_now);

    char buffer[80];
    std::strftime(buffer, 80, "%H_%M_%S", &local_tm);
    std::string filename = std::string(dir) + "/" + buffer + ".bmp";
    SaveTextureToFile((ID3D11Texture2D *)texture, filename);
  }
}

extern "C" void *dxgi_capture(void *capturer, int wait_ms) {
  DemoApplication *d = (DemoApplication *)capturer;
  void *texture = d->Capture(wait_ms);
#if 1
  save_bmp(texture);
#endif
  return texture;
}

extern "C" void destroy_dxgi_capturer(void *capturer) {
  DemoApplication *d = (DemoApplication *)capturer;
  if (d)
    delete d;
}