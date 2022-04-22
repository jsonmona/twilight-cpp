#include "D3DTextureUploader.h"

TWILIGHT_DEFINE_LOGGER(D3DTextureUploader);

D3DTextureUploader::D3DTextureUploader() : width(-1), height(-1) {}

D3DTextureUploader::~D3DTextureUploader() {}

void D3DTextureUploader::init(DxgiHelper dxgiHelper_, D3D11Device device_) {
    staging.release();
    texture.release();
    context.release();

    dxgiHelper = std::move(dxgiHelper_);
    device = std::move(device_);
    device->GetImmediateContext(context.data());
}

D3D11Texture2D D3DTextureUploader::upload(const TextureSoftware& input) {
    HRESULT hr;

    if (width != input.width || height != input.height) {
        staging.release();
        texture.release();
        width = input.width;
        height = input.height;
    }

    if (staging.isInvalid()) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Width = width;
        desc.Height = height;
        desc.ArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = device->CreateTexture2D(&desc, nullptr, staging.data());
        log.assert_quit(SUCCEEDED(hr), "Failed to create staging texture");
    }

    if (texture.isInvalid()) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Width = width;
        desc.Height = height;
        desc.ArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        hr = device->CreateTexture2D(&desc, nullptr, texture.data());
        log.assert_quit(SUCCEEDED(hr), "Failed to create texture");
    }

    D3D11_MAPPED_SUBRESOURCE mapInfo;
    hr = context->Map(staging.ptr(), 0, D3D11_MAP_WRITE, 0, &mapInfo);
    log.assert_quit(SUCCEEDED(hr), "Failed to map staging texture");

    uint8_t* dst = reinterpret_cast<uint8_t*>(mapInfo.pData);
    uint8_t* src = input.data[0];

    // TODO: Test and enable fast path
    if (mapInfo.RowPitch != input.linesize[0] || true) {
        for (int i = 0; i < height; i++) {
            memcpy(dst, src, width * 4);
            dst += mapInfo.RowPitch;
            src += input.linesize[0];
        }
    } else {
        // Fast path
        memcpy(dst, src, mapInfo.RowPitch * height);
    }

    context->Unmap(staging.ptr(), 0);

    context->CopySubresourceRegion(texture.ptr(), 0, 0, 0, 0, staging.ptr(), 0, nullptr);

    return texture;
}
