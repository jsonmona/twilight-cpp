#ifndef TWILIGHT_SERVER_PLATFORM_WINDOWS_SCALED3D_H
#define TWILIGHT_SERVER_PLATFORM_WINDOWS_SCALED3D_H

#include "common/DesktopFrame.h"
#include "common/log.h"

#include "common/platform/windows/DxgiHelper.h"

#include <memory>

class ScaleD3D {
public:
    static std::unique_ptr<ScaleD3D> createInstance(int w, int h, ScaleType type);

    virtual ~ScaleD3D();

    virtual void init(const D3D11Device& device, const D3D11DeviceContext& context);

    void getRatio(Rational* xRatio, Rational* yRatio);

    void pushInput(const D3D11Texture2D& tex);
    D3D11Texture2D popOutput();

protected:
    static NamedLogger log;

    D3D11Device device;
    D3D11DeviceContext context;
    ScaleType outType;

    int inWidth, inHeight;
    DXGI_FORMAT inFormat;

    int outWidth, outHeight;
    DXGI_FORMAT outFormat;

    D3D11Buffer vertexBuffer, cbuffer;
    D3D11VertexShader vertexShader;
    D3D11InputLayout inputLayout;
    D3D11ShaderResourceView srInput;
    D3D11SamplerState clampSampler;
    D3D11Texture2D outputTex;

    explicit ScaleD3D(int w, int h, ScaleType _outType);

    virtual void convert_(const D3D11Texture2D& inputTex) = 0;
};

#endif
