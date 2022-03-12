#include "ScaleD3D.h"

#include "common/platform/windows/ComWrapper.h"
#include "common/util.h"

#include <cassert>
#include <limits>
#include <string>
#include <vector>

static const float quadVertex[] = {-1, -1, -1, 1, 1, -1, 1, 1};
static const UINT quadVertexStride = 2 * sizeof(quadVertex[0]);
static const UINT quadVertexOffset = 0;
static const UINT quadVertexCount = 4;

class ScaleD3D_AYUV : public ScaleD3D {
    D3D11RenderTargetView rtOutput;
    D3D11PixelShader pixelShader;

protected:
    void convert_(const D3D11Texture2D& inputTex) override;

public:
    ScaleD3D_AYUV(int w, int h) : ScaleD3D(createNamedLogger("ScaleD3D_AYUV"), w, h, ScaleType::AYUV) {}

    void init(const D3D11Device& device, const D3D11DeviceContext& context) override;
};

class ScaleD3D_NV12 : public ScaleD3D {
    D3D11Texture2D chromaLargeTex;
    D3D11ShaderResourceView srChromaLarge;
    D3D11RenderTargetView rtLuma, rtChroma, rtChromaLarge;
    D3D11PixelShader pixelShaderY, pixelShaderUV, pixelShaderCopy;

protected:
    void convert_(const D3D11Texture2D& inputTex) override;

public:
    ScaleD3D_NV12(int w, int h) : ScaleD3D(createNamedLogger("ScaleD3D_NV12"), w, h, ScaleType::NV12) {}

    void init(const D3D11Device& device, const D3D11DeviceContext& context) override;
};

std::unique_ptr<ScaleD3D> ScaleD3D::createInstance(int w, int h, ScaleType type) {
    std::unique_ptr<ScaleD3D> ret;

    if (type == ScaleType::AYUV)
        ret = std::make_unique<ScaleD3D_AYUV>(w, h);
    else if (type == ScaleType::NV12)
        ret = std::make_unique<ScaleD3D_NV12>(w, h);
    else
        error_quit(createNamedLogger("ScaleD3D"), "Invalid surface type requested");

    return ret;
}

ScaleD3D::ScaleD3D(LoggerPtr logger, int w, int h, ScaleType type)
    : log(logger),
      outType(type),
      outWidth(w),
      outHeight(h),
      inFormat(DXGI_FORMAT_UNKNOWN),
      outFormat(DXGI_FORMAT_UNKNOWN) {}

ScaleD3D::~ScaleD3D() {}

void ScaleD3D::init(const D3D11Device& device, const D3D11DeviceContext& context) {
    HRESULT hr;

    outputTex.release();
    vertexBuffer.release();
    cbuffer.release();
    clampSampler.release();
    vertexShader.release();
    inputLayout.release();

    this->device = device;
    this->context = context;

    D3D11_TEXTURE2D_DESC outDesc = {};
    outDesc.Width = outWidth;
    outDesc.Height = outHeight;
    outDesc.Format = outFormat;
    outDesc.Usage = D3D11_USAGE_DEFAULT;
    outDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    outDesc.MipLevels = 1;
    outDesc.ArraySize = 1;
    outDesc.SampleDesc.Count = 1;
    device->CreateTexture2D(&outDesc, nullptr, outputTex.data());

    D3D11_BUFFER_DESC vertexBufferDesc = {};
    vertexBufferDesc.ByteWidth = sizeof(quadVertex);
    vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vertexBufferData = {};
    vertexBufferData.pSysMem = quadVertex;
    device->CreateBuffer(&vertexBufferDesc, &vertexBufferData, vertexBuffer.data());

    float Kr, Kg, Kb;

    // Bt.709
    Kb = 0.0722f;
    Kr = 0.2126f;
    Kg = 1 - Kr - Kb;

    // RGB -> YPbPr matrix transposed
    // clang-format off
    float mat[4][4] = {
        Kr, -0.5f * Kr / (1 - Kb),  0.5f,                 0,
        Kg, -0.5f * Kg / (1 - Kb), -0.5f * Kg / (1 - Kr), 0,
        Kb,  0.5f,                 -0.5f * Kb / (1 - Kr), 0,
        0,   0,                     0,                    0
    };
    // clang-format on

    D3D11_BUFFER_DESC cbufferDesc = {};
    cbufferDesc.ByteWidth = sizeof(mat);
    cbufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    cbufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA cbufferData = {};
    cbufferData.pSysMem = mat;
    device->CreateBuffer(&cbufferDesc, &cbufferData, cbuffer.data());

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = -FLT_MAX;
    samplerDesc.MaxLOD = FLT_MAX;
    device->CreateSamplerState(&samplerDesc, clampSampler.data());

    // FIXME: Unchecked std::optional unwrapping
    ByteBuffer vertexBlob = loadEntireFile("rgb2yuv-vs_main.fxc").value();
    hr = device->CreateVertexShader(vertexBlob.data(), vertexBlob.size(), nullptr, vertexShader.data());
    check_quit(FAILED(hr), log, "Failed to create vertex shader");

    D3D11_INPUT_ELEMENT_DESC inputLayoutDesc[] = {
        {"POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    device->CreateInputLayout(inputLayoutDesc, 1, vertexBlob.data(), vertexBlob.size(), inputLayout.data());
}

void ScaleD3D::pushInput(const D3D11Texture2D& inputTex) {
    D3D11_TEXTURE2D_DESC desc;
    inputTex->GetDesc(&desc);
    inWidth = desc.Width;
    inHeight = desc.Height;
    inFormat = desc.Format;
    check_quit((desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0, log,
               "Input texture does not have D3D11_BIND_SHADER_RESOURCE");

    srInput.release();

    D3D11_SHADER_RESOURCE_VIEW_DESC srDesc = {};
    srDesc.Format = inFormat;
    srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srDesc.Texture2D.MostDetailedMip = 0;
    srDesc.Texture2D.MipLevels = -1;
    device->CreateShaderResourceView(inputTex.ptr(), &srDesc, srInput.data());

    convert_(inputTex);
}

D3D11Texture2D ScaleD3D::popOutput() {
    D3D11Texture2D tex;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Format = outFormat;
    desc.Width = outWidth;
    desc.Height = outHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = 0;

    device->CreateTexture2D(&desc, nullptr, tex.data());
    context->CopyResource(tex.ptr(), outputTex.ptr());
    return tex;
}

void ScaleD3D_AYUV::init(const D3D11Device& device, const D3D11DeviceContext& context) {
    outFormat = DXGI_FORMAT_AYUV;
    ScaleD3D::init(device, context);

    pixelShader.release();
    rtOutput.release();

    // FIXME: Unchecked std::optional unwrapping
    ByteBuffer blob = loadEntireFile("rgb2yuv-ps_yuv.fxc").value();
    device->CreatePixelShader(blob.data(), blob.size(), nullptr, pixelShader.data());

    D3D11_RENDER_TARGET_VIEW_DESC rtDesc = {};
    rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtDesc.Texture2D.MipSlice = 0;
    device->CreateRenderTargetView(outputTex.ptr(), &rtDesc, rtOutput.data());
}

void ScaleD3D_AYUV::convert_(const D3D11Texture2D& inputTex) {
    D3D11_VIEWPORT viewport = {0, 0, (float)(outWidth), (float)(outHeight), 0, 1};
    context->RSSetViewports(1, &viewport);

    context->VSSetShader(vertexShader.ptr(), nullptr, 0);
    context->PSSetShader(pixelShader.ptr(), nullptr, 0);

    context->PSSetConstantBuffers(0, 1, cbuffer.data());
    context->PSSetShaderResources(0, 1, srInput.data());
    context->PSSetSamplers(0, 1, clampSampler.data());
    context->OMSetRenderTargets(1, rtOutput.data(), nullptr);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->IASetInputLayout(inputLayout.ptr());
    context->IASetVertexBuffers(0, 1, vertexBuffer.data(), &quadVertexStride, &quadVertexOffset);
    context->Draw(quadVertexCount, 0);
}

void ScaleD3D_NV12::init(const D3D11Device& device, const D3D11DeviceContext& context) {
    outFormat = DXGI_FORMAT_NV12;
    if (outWidth % 2 != 0 || outHeight % 2 != 0)
        error_quit(log, "Dimension not multiple of 2 when using NV12 format");

    ScaleD3D::init(device, context);

    pixelShaderY.release();
    pixelShaderUV.release();
    pixelShaderCopy.release();
    chromaLargeTex.release();
    srChromaLarge.release();
    rtChromaLarge.release();
    rtLuma.release();
    rtChroma.release();

    // FIXME: Unchecked std::optional unwrapping
    ByteBuffer blob = loadEntireFile("rgb2yuv-ps_y.fxc").value();
    device->CreatePixelShader(blob.data(), blob.size(), nullptr, pixelShaderY.data());

    blob = loadEntireFile("rgb2yuv-ps_uv.fxc").value();
    device->CreatePixelShader(blob.data(), blob.size(), nullptr, pixelShaderUV.data());

    blob = loadEntireFile("rgb2yuv-ps_copy.fxc").value();
    device->CreatePixelShader(blob.data(), blob.size(), nullptr, pixelShaderCopy.data());

    D3D11_TEXTURE2D_DESC chromaLargeDesc = {};
    chromaLargeDesc.Width = outWidth;
    chromaLargeDesc.Height = outHeight;
    chromaLargeDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    chromaLargeDesc.Usage = D3D11_USAGE_DEFAULT;
    chromaLargeDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    chromaLargeDesc.MipLevels = 1;
    chromaLargeDesc.ArraySize = 1;
    chromaLargeDesc.SampleDesc.Count = 1;
    device->CreateTexture2D(&chromaLargeDesc, nullptr, chromaLargeTex.data());

    D3D11_SHADER_RESOURCE_VIEW_DESC srChromaLargeDesc = {};
    srChromaLargeDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    srChromaLargeDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srChromaLargeDesc.Texture2D.MostDetailedMip = 0;
    srChromaLargeDesc.Texture2D.MipLevels = -1;
    device->CreateShaderResourceView(chromaLargeTex.ptr(), &srChromaLargeDesc, srChromaLarge.data());

    D3D11_RENDER_TARGET_VIEW_DESC rtChromaLargeDesc = {};
    rtChromaLargeDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    rtChromaLargeDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtChromaLargeDesc.Texture2D.MipSlice = 0;
    device->CreateRenderTargetView(chromaLargeTex.ptr(), &rtChromaLargeDesc, rtChromaLarge.data());

    D3D11_RENDER_TARGET_VIEW_DESC rtDesc = {};
    rtDesc.Format = DXGI_FORMAT_R8_UNORM;
    rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtDesc.Texture2D.MipSlice = 0;
    device->CreateRenderTargetView(outputTex.ptr(), &rtDesc, rtLuma.data());

    D3D11_RENDER_TARGET_VIEW_DESC rtChromaDesc = {};
    rtChromaDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    rtChromaDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtChromaDesc.Texture2D.MipSlice = 0;
    device->CreateRenderTargetView(outputTex.ptr(), &rtChromaDesc, rtChroma.data());
}

void ScaleD3D_NV12::convert_(const D3D11Texture2D& inputTex) {
    D3D11_VIEWPORT viewport = {0, 0, (float)(outWidth), (float)(outHeight), 0, 1};
    context->RSSetViewports(1, &viewport);

    // Render UV component (large)
    context->VSSetShader(vertexShader.ptr(), nullptr, 0);
    context->PSSetShader(pixelShaderUV.ptr(), nullptr, 0);

    context->PSSetConstantBuffers(0, 1, cbuffer.data());
    context->PSSetShaderResources(0, 1, srInput.data());
    context->PSSetSamplers(0, 1, clampSampler.data());
    context->OMSetRenderTargets(1, rtChromaLarge.data(), nullptr);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->IASetInputLayout(inputLayout.ptr());
    context->IASetVertexBuffers(0, 1, vertexBuffer.data(), &quadVertexStride, &quadVertexOffset);
    context->Draw(quadVertexCount, 0);

    // render Y component
    context->PSSetShader(pixelShaderY.ptr(), nullptr, 0);
    context->OMSetRenderTargets(1, rtLuma.data(), nullptr);
    context->Draw(quadVertexCount, 0);

    // render UV component (small)
    D3D11_VIEWPORT smallViewport = {0, 0, (float)(outWidth / 2), (float)(outHeight / 2), 0, 1};
    context->RSSetViewports(1, &smallViewport);

    context->PSSetShader(pixelShaderCopy.ptr(), nullptr, 0);
    context->PSSetShaderResources(0, 1, srChromaLarge.data());
    context->OMSetRenderTargets(1, rtChroma.data(), nullptr);
    context->Draw(quadVertexCount, 0);
}
