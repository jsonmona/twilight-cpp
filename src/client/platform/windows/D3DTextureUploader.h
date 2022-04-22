#ifndef TWILIGHT_CLIENT_PLATFORM_WINDOWS_D3DTEXTUREUPLOADER_H
#define TWILIGHT_CLIENT_PLATFORM_WINDOWS_D3DTEXTUREUPLOADER_H

#include "common/DesktopFrame.h"

#include "common/platform/software/TextureSoftware.h"

#include "common/platform/windows/DxgiHelper.h"

class D3DTextureUploader {
public:
    D3DTextureUploader();
    D3DTextureUploader(const D3DTextureUploader& copy) = delete;
    D3DTextureUploader(D3DTextureUploader&& move) = delete;

    ~D3DTextureUploader();

    void init(DxgiHelper dxgiHelper, D3D11Device device);

    D3D11Texture2D upload(const TextureSoftware& input);

private:
    static NamedLogger log;

    int width, height;

    DxgiHelper dxgiHelper;
    D3D11Device device;
    D3D11DeviceContext context;
    D3D11Texture2D staging;
    D3D11Texture2D texture;
};

#endif
