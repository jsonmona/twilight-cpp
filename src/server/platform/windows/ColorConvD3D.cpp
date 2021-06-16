#include "ColorConvD3D.h"

#include "stdafx.h"
#include "common/platform/windows/ComWrapper.h"

#include <cassert>
#include <vector>
#include <string>
#include <limits>


static const float quadVertex[] = {
    -1, -1,
    -1,  1,
     1, -1,
     1,  1
};
static const UINT quadVertexStride = 2 * sizeof(quadVertex[0]);
static const UINT quadVertexOffset = 0;
static const UINT quadVertexCount = 4;


// load entire file
static std::vector<uint8_t> loadFile(const wchar_t* path) {
    HANDLE f = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        abort();

    static_assert(sizeof(LARGE_INTEGER) == sizeof(long long), "LARGE_INTEGER must be long long!");
    long long fileSize;
    if(!GetFileSizeEx(f, (LARGE_INTEGER*) &fileSize))
        abort();

    // unlikely.
    if (std::numeric_limits<size_t>::max() < fileSize)
        abort();

    std::vector<uint8_t> result(fileSize);

    DWORD readPtr = 0;
    while (readPtr < fileSize) {
        if (!ReadFile(f, result.data() + readPtr, fileSize - readPtr, &readPtr, nullptr))
            abort();
    }

    CloseHandle(f);

    return result;
}


class ColorConvD3D_AYUV : public ColorConvD3D {
    D3D11RenderTargetView rtOutput;
    D3D11PixelShader pixelShader;

protected:
    void _reconfigure() override;
    void _convert() override;

public:
    void init(const D3D11Device& device, const D3D11DeviceContext& context) override;
};

class ColorConvD3D_NV12 : public ColorConvD3D {
    D3D11Texture2D chromaLargeTex;
    D3D11ShaderResourceView srChromaLarge;
    D3D11RenderTargetView rtLuma, rtChroma, rtChromaLarge;
    D3D11PixelShader pixelShaderY, pixelShaderUV, pixelShaderCopy;

protected:
    void _reconfigure() override;
    void _convert() override;

public:
    void init(const D3D11Device& device, const D3D11DeviceContext& context) override;
};


std::unique_ptr<ColorConvD3D> ColorConvD3D::createInstance(Type in, Type out, Color color) {
    if (in != Type::RGB || out == Type::RGB)
        abort();  // This configuration is not supported (yet)

    std::unique_ptr<ColorConvD3D> ret;

    if(out == Type::AYUV)
        ret = std::make_unique<ColorConvD3D_AYUV>();
    else if(out == Type::NV12)
        ret = std::make_unique<ColorConvD3D_NV12>();
    else
        abort();  // Invalid type

    ret->inType = in;
    ret->outType = out;
    ret->color = color;
    return ret;
}


ColorConvD3D::ColorConvD3D() : width(-1), height(-1), inputFormat(DXGI_FORMAT_UNKNOWN), outputFormat(DXGI_FORMAT_UNKNOWN) {}

ColorConvD3D::~ColorConvD3D() {}

void ColorConvD3D::init(const D3D11Device& device, const D3D11DeviceContext& context) {
    HRESULT hr;

    this->device = device;
    this->context = context;

    dirty = true;
    vertexBuffer.release();
    cbuffer.release();
    clampSampler.release();
    vertexShader.release();
    inputLayout.release();

    D3D11_BUFFER_DESC vertexBufferDesc = {};
    vertexBufferDesc.ByteWidth = sizeof(quadVertex);
    vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vertexBufferData = {};
    vertexBufferData.pSysMem = quadVertex;
    device->CreateBuffer(&vertexBufferDesc, &vertexBufferData, vertexBuffer.data());

    float Kr, Kg, Kb;
    if (color == Color::BT601) {
        Kb = 0.114f;
        Kr = 0.229f;
    }
    else if (color == Color::BT709) {
        Kb = 0.0722f;
        Kr = 0.2126f;
    }
    else if (color == Color::BT2020) {
        abort();  // Not sure if this is correct (8 bits vs 10 bits?)
        Kb = 0.0593f;
        Kr = 0.2627f;
    }
    else {
        // invalid format
        abort();
    }
    Kg = 1 - Kr - Kb;

    // RGB -> YPbPr matrix transposed
    float mat[4][4] = {
        Kr, -0.5f * Kr / (1 - Kb), 0.5f, 0,
        Kg, -0.5f * Kg / (1 - Kb), -0.5f * Kg / (1 - Kr), 0,
        Kb, 0.5f, -0.5f * Kb / (1 - Kr), 0,
        0, 0, 0, 0
    };

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

    std::vector<uint8_t> vertexBlob = loadFile(L"rgb2yuv-vs_main.fxc");
    hr = device->CreateVertexShader(vertexBlob.data(), vertexBlob.size(), nullptr, vertexShader.data());
    if (FAILED(hr))
        abort();

    D3D11_INPUT_ELEMENT_DESC inputLayoutDesc[] = {
        {"POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    device->CreateInputLayout(inputLayoutDesc, 1, vertexBlob.data(), vertexBlob.size(), inputLayout.data());
}

void ColorConvD3D::_reconfigure() {
    D3D11_TEXTURE2D_DESC inDesc = {}, outDesc = {};

    inDesc.Width = width;
    inDesc.Height = height;
    inDesc.Format = inputFormat;
    inDesc.Usage = D3D11_USAGE_DEFAULT;
    inDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    inDesc.MipLevels = 1;
    inDesc.ArraySize = 1;
    inDesc.SampleDesc.Count = 1;
    device->CreateTexture2D(&inDesc, nullptr, inputTex.data());

    outDesc.Width = width;
    outDesc.Height = height;
    outDesc.Format = outputFormat;
    outDesc.Usage = D3D11_USAGE_DEFAULT;
    outDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    outDesc.MipLevels = 1;
    outDesc.ArraySize = 1;
    outDesc.SampleDesc.Count = 1;
    device->CreateTexture2D(&outDesc, nullptr, outputTex.data());

    D3D11_SHADER_RESOURCE_VIEW_DESC srDesc = {};
    srDesc.Format = inputFormat;
    srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srDesc.Texture2D.MostDetailedMip = 0;
    srDesc.Texture2D.MipLevels = -1;
    device->CreateShaderResourceView(inputTex.ptr(), &srDesc, srInput.data());
}

bool ColorConvD3D::_checkNeedsReconfigure(const D3D11Texture2D& tex) {
    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);

    bool cond = false;
    cond = cond || desc.Width != width;
    cond = cond || desc.Height != height;
    cond = cond || desc.Format != inputFormat;

    if (cond) {
        width = desc.Width;
        height = desc.Height;
        inputFormat = desc.Format;
        return true;
    }

    return false;
}

void ColorConvD3D::pushInput(const D3D11Texture2D& tex) {
    if (_checkNeedsReconfigure(tex))
        _reconfigure();

    context->CopyResource(inputTex.ptr(), tex.ptr());
    dirty = true;
}

D3D11Texture2D ColorConvD3D::popOutput() {
    if (dirty)
        _convert();

    D3D11Texture2D tex;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Format = outputFormat;
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_VIDEO_ENCODER;

    device->CreateTexture2D(&desc, nullptr, tex.data());
    context->CopyResource(tex.ptr(), outputTex.ptr());
    return tex;
}


void ColorConvD3D_AYUV::init(const D3D11Device& device, const D3D11DeviceContext& context) {
    ColorConvD3D::init(device, context);
    outputFormat = DXGI_FORMAT_AYUV;

    pixelShader.release();

    std::vector<uint8_t> blob = loadFile(L"rgb2yuv-ps_yuv.fxc");
    device->CreatePixelShader(blob.data(), blob.size(), nullptr, pixelShader.data());
}

void ColorConvD3D_AYUV::_reconfigure() {
    ColorConvD3D::_reconfigure();

    D3D11_RENDER_TARGET_VIEW_DESC rtDesc = {};
    rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtDesc.Texture2D.MipSlice = 0;
    device->CreateRenderTargetView(outputTex.ptr(), &rtDesc, rtOutput.data());
}

void ColorConvD3D_AYUV::_convert() {
    D3D11_VIEWPORT viewport = {
        0, 0,
        (float)(width), (float)(height),
        0, 1
    };
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


void ColorConvD3D_NV12::init(const D3D11Device& device, const D3D11DeviceContext& context) {
    ColorConvD3D::init(device, context);
    outputFormat = DXGI_FORMAT_NV12;

    pixelShaderY.release();
    pixelShaderUV.release();
    pixelShaderCopy.release();

    std::vector<uint8_t> blob = loadFile(L"rgb2yuv-ps_y.fxc");
    device->CreatePixelShader(blob.data(), blob.size(), nullptr, pixelShaderY.data());

    blob = loadFile(L"rgb2yuv-ps_uv.fxc");
    device->CreatePixelShader(blob.data(), blob.size(), nullptr, pixelShaderUV.data());

    blob = loadFile(L"rgb2yuv-ps_copy.fxc");
    device->CreatePixelShader(blob.data(), blob.size(), nullptr, pixelShaderCopy.data());
}

void ColorConvD3D_NV12::_reconfigure() {
    ColorConvD3D::_reconfigure();

    if (width % 2 != 0 || height % 2 != 0) {
        // Invalid dimension
        abort();  // FIXME: abort
    }

    D3D11_TEXTURE2D_DESC chromaLargeDesc = {};
    chromaLargeDesc.Width = width;
    chromaLargeDesc.Height = height;
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

void ColorConvD3D_NV12::_convert() {
    D3D11_VIEWPORT viewport = {
        0, 0,
        (float)(width), (float)(height),
        0, 1
    };
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
    D3D11_VIEWPORT smallViewport = {
        0, 0,
        (float)(width / 2), (float)(height / 2),
        0, 1
    };
    context->RSSetViewports(1, &smallViewport);

    context->PSSetShader(pixelShaderCopy.ptr(), nullptr, 0);
    context->PSSetShaderResources(0, 1, srChromaLarge.data());
    context->OMSetRenderTargets(1, rtChroma.data(), nullptr);
    context->Draw(quadVertexCount, 0);
}
