#ifndef TWILIGHT_CLIENT_PLATFORM_WINDOWS_DECODEPIPELINESOFTD3D_H
#define TWILIGHT_CLIENT_PLATFORM_WINDOWS_DECODEPIPELINESOFTD3D_H

#include "common/log.h"
#include "common/DesktopFrame.h"

#include "common/platform/software/ScaleSoftware.h"

#include "common/platform/windows/DxgiHelper.h"

#include "client/platform/software/IDecoderSoftware.h"

#include "client/platform/windows/D3DTextureUploader.h"
#include "client/platform/windows/RendererD3D.h"

class DecodePipelineSoftD3D {
public:
    explicit DecodePipelineSoftD3D(std::unique_ptr<IDecoderSoftware> decoder);
    DecodePipelineSoftD3D(const DecodePipelineSoftD3D& copy) = delete;
    DecodePipelineSoftD3D(DecodePipelineSoftD3D&& move) = delete;

    ~DecodePipelineSoftD3D();

    void setInputResolution(int width, int height);
    void setOutputResolution(int width, int height);

    void pushData(DesktopFrame<ByteBuffer>&& frame);

    // Returns true if a new frame was drawn
    bool render(RendererD3D* renderer, DesktopFrame<D3D11Texture2D>* frame);

    void start();
    void stop();

    IDecoderSoftware* getDecoder() const { return decoder.get(); }
    DxgiHelper getDxgiHelper() const { return dxgiHelper; }
    D3D11Device getDevice() const { return device; }

private:
    bool readD3D_(DesktopFrame<D3D11Texture2D>* output);

    static NamedLogger log;

    DxgiHelper dxgiHelper;
    D3D11Device device;
    D3D11DeviceContext context;

    std::unique_ptr<IDecoderSoftware> decoder;
    D3DTextureUploader uploader;
    ScaleSoftware scale;
};

#endif
