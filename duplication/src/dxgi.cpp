#include <DDA.h>
#include <string>

bool SaveTextureToFile(ID3D11Texture2D* texture, const std::string& filename)
{   
    if (!texture) return false;

    // 获取设备和设备上下文
    ID3D11Device* device = nullptr;
    texture->GetDevice(&device);
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);

    // 创建系统内存中的纹理
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* stagingTexture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &stagingTexture);
    if (FAILED(hr))
    {
        return false;
    }

    // 复制纹理数据到系统内存中
    context->CopyResource(stagingTexture, texture);

    // 从系统内存中读取纹理数据
    D3D11_MAPPED_SUBRESOURCE resource;
    hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &resource);
    if (FAILED(hr))
    {
        stagingTexture->Release();
        return false;
    }

    // 写入 BMP 文件头部数据
    BITMAPFILEHEADER bmpHeader;
    bmpHeader.bfType = 0x4d42;
    bmpHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmpHeader.bfSize = bmpHeader.bfOffBits + resource.RowPitch * desc.Height;
    bmpHeader.bfReserved1 = 0;
    bmpHeader.bfReserved2 = 0;

    // 写入 BMP 信息头部数据
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

    // 打开文件并写入 BMP 文件头部和信息头部数据
    std::ofstream file(filename, std::ios::out | std::ios::binary);
    if (!file.is_open())
    {
        context->Unmap(stagingTexture, 0);
        stagingTexture->Release();
        return false;
    }
    file.write(reinterpret_cast<const char*>(&bmpHeader), sizeof(BITMAPFILEHEADER));
    file.write(reinterpret_cast<const char*>(&bmpInfoHeader), sizeof(BITMAPINFOHEADER));

    // 写入像素数据
    char* pixels = static_cast<char*>(resource.pData);
    for (UINT row = 0; row < desc.Height; ++row)
    {
        file.write(pixels, desc.Width * 4);
        pixels += resource.RowPitch;
    }

    // 关闭文件
    file.close();

    // 解除映射和释放资源
    context->Unmap(stagingTexture, 0);
    stagingTexture->Release();

    return true;
}

extern "C" void* dxgi_new_duplicator()
{
    DemoApplication *d = new DemoApplication();
    HRESULT hr = d->Init();
    if (FAILED(hr)) {
        delete d;
        d = NULL;
        return NULL;
    }

    return d;
}

extern "C" void* dxgi_device(void *self) {
    DemoApplication *d = (DemoApplication *)self;
    return d->Device();
}

extern "C" void* dxgi_duplicate(void *self, int wait_ms)
{
    DemoApplication *d = (DemoApplication *)self;
    void *result =  d->Capture(wait_ms);
#if 0
    static int index = 0;
    if (index++ % 2000 ==0) {
        std::string filename = "D:\\tmp\\bmp\\" + std::to_string(index) + ".bmp";
        SaveTextureToFile((ID3D11Texture2D*)result, filename);
    }
#endif
    return result;
}

extern "C" void destroy_dxgi_duplicator(void *self)
{
    DemoApplication *d = (DemoApplication *)self;
    if (d) delete d;
}