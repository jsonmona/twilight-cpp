#ifndef SERVER_PLATFORM_WINDOWS_SCALE_D3D_H_
#define SERVER_PLATFORM_WINDOWS_SCALE_D3D_H_


#include "common/log.h"

#include "server/CaptureData.h"
#include "server/platform/windows/DeviceManagerD3D.h"

#include <memory>


class ScaleD3D {
protected:
	LoggerPtr log;

	D3D11Device device;
	D3D11DeviceContext context;
	ScaleType outType;
	bool dirty = true;

	int inWidth, inHeight;
	DXGI_FORMAT inFormat;

	int outWidth, outHeight;
	DXGI_FORMAT outFormat;

	D3D11Buffer vertexBuffer, cbuffer;
	D3D11VertexShader vertexShader;
	D3D11InputLayout inputLayout;
	D3D11SamplerState clampSampler;
	D3D11Texture2D inputTex;
	D3D11Texture2D outputTex;
	D3D11ShaderResourceView srInput;

	explicit ScaleD3D(LoggerPtr logger, int w, int h, ScaleType _outType);

	bool _checkNeedsReconfigure(const D3D11Texture2D& tex);
	virtual void _reconfigure();
	virtual void _convert() = 0;

public:
	static std::unique_ptr<ScaleD3D> createInstance(int w, int h, ScaleType type);

	virtual ~ScaleD3D();
	virtual void init(const D3D11Device& device, const D3D11DeviceContext& context);

	void pushInput(const D3D11Texture2D& tex);
	D3D11Texture2D popOutput();
};


#endif