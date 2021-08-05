#ifndef CLIENT_PLATFORM_WINDOWS_STREAM_VIEWER_D3D_H_
#define CLIENT_PLATFORM_WINDOWS_STREAM_VIEWER_D3D_H_


#include "common/log.h"
#include "common/platform/windows/ComWrapper.h"

#include "client/StreamViewerBase.h"
#include "client/platform/software/DecoderSoftware.h"

#include <packet.pb.h>

#include <thread>


struct FrameData {
	int cursorX, cursorY;
	TextureSoftware desktop;
	std::shared_ptr<ByteBuffer> cursorDataUpdate;
	int cursorWidth, cursorHeight;
};


class StreamViewerD3D : public StreamViewerBase {
	Q_OBJECT;
	LoggerPtr log;

	std::atomic<bool> flagInitialized = false;
	std::atomic<bool> flagRunRender = false;

	std::thread renderThread;

	DxgiFactory5 dxgiFactory;
	D3D11Device device;
	D3D11DeviceContext context;
	ComWrapper<IDXGISwapChain1> swapChain;
	D3D11RenderTargetView framebufferRTV;
	D3D11Texture2D desktopTex;
	D3D11Texture2D cursorTex;
	D3D11VertexShader vertexShaderFull;
	D3D11VertexShader vertexShaderBox;
	D3D11PixelShader pixelShader;
	D3D11SamplerState clampSampler;
	D3D11InputLayout inputLayoutFull;
	D3D11InputLayout inputLayoutBox;
	D3D11Buffer vertexBuffer;
	D3D11Buffer cbuffer;
	D3D11ShaderResourceView desktopSRV;
	D3D11ShaderResourceView cursorSRV;
	D3D11BlendState blendState;

	std::unique_ptr<DecoderSoftware> decoder;
	int width, height;
	int cursorTexWidth, cursorTexHeight;
	std::mutex frameDataLock;
	std::deque<FrameData> frameData;
	std::deque<FrameData> undecodedFrameData; //FIXME: Assumes dts to increase monotonically

	HWND hwnd() const { return reinterpret_cast<HWND>(winId()); }
	void _onNewFrame(TextureSoftware&& frame);
	void _init();
	void _recreateCursorTexture();
	void _renderLoop();

protected:
	bool useAbsCursor() { return false; }
	void processNewPacket(const msg::Packet& pkt, uint8_t* extraData) override;
	void resizeEvent(QResizeEvent* ev) override;

public:
	StreamViewerD3D();
	~StreamViewerD3D() override;

	QPaintEngine* paintEngine() const override { return nullptr; }
};


#endif