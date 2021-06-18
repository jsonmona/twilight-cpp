#ifndef COLOR_SPACE_CONV_H_
#define COLOR_SPACE_CONV_H_


#include "common/platform/windows/ComWrapper.h"
#include "common/log.h"

#include <memory>


class ColorConvD3D {
public:
	enum class Type {
		RGB,
		AYUV,
		NV12
	};
	enum class Color {
		BT601,
		BT709,
		BT2020
	};

protected:
	LoggerPtr log;

	D3D11Device device;
	D3D11DeviceContext context;
	Color color;
	Type inType, outType;
	bool dirty = true;

	int width = -1, height = -1;
	DXGI_FORMAT inputFormat;
	DXGI_FORMAT outputFormat;

	D3D11Buffer vertexBuffer, cbuffer;
	D3D11VertexShader vertexShader;
	D3D11InputLayout inputLayout;
	D3D11SamplerState clampSampler;
	D3D11Texture2D inputTex;
	D3D11Texture2D outputTex;
	D3D11ShaderResourceView srInput;

	bool _checkNeedsReconfigure(const D3D11Texture2D& tex);
	virtual void _reconfigure();
	virtual void _convert() = 0;

public:
	static std::unique_ptr<ColorConvD3D> createInstance(Type in, Type out, Color color);

	ColorConvD3D(LoggerPtr logger);
	virtual ~ColorConvD3D();
	virtual void init(const D3D11Device& device, const D3D11DeviceContext& context);

	void pushInput(const D3D11Texture2D& tex);
	D3D11Texture2D popOutput();
};


#endif