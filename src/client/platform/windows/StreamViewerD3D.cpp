#include "StreamViewerD3D.h"

#include <vector>


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
	std::vector<uint8_t> result(0);

	HANDLE f = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE)
		return result;

	static_assert(sizeof(LARGE_INTEGER) == sizeof(long long), "LARGE_INTEGER must be long long!");
	long long fileSize;
	if (!GetFileSizeEx(f, (LARGE_INTEGER*)&fileSize))
		return result;

	// unlikely.
	if (std::numeric_limits<size_t>::max() < fileSize)
		return result;

	result.resize(fileSize);

	DWORD readPtr = 0;
	while (readPtr < fileSize) {
		if (!ReadFile(f, result.data() + readPtr, fileSize - readPtr, &readPtr, nullptr)) {
			result.resize(0);
			return result;
		}
	}

	CloseHandle(f);
	return result;
}


StreamViewerD3D::StreamViewerD3D() :
	StreamViewerBase(), log(createNamedLogger("StreamViewerD3D"))
{
	width = 1920;
	height = 1080;
}

StreamViewerD3D::~StreamViewerD3D() {
	decoder->stop();

	flagRunRender.store(false, std::memory_order_release);
	renderThread.join();
}

void StreamViewerD3D::resizeEvent(QResizeEvent* ev) {
	StreamViewerBase::resizeEvent(ev);

	bool wasInitialized = flagInitialized.exchange(true, std::memory_order_seq_cst);
	if (!wasInitialized) {
		_init();
	}
}

void StreamViewerD3D::processNewPacket(const msg::Packet& pkt, uint8_t* extraData) {
	while (!flagRunRender.load(std::memory_order_acquire))
		Sleep(0);

	static std::shared_ptr<ByteBuffer> pendingCursorChange;
	static int cursorWidth, cursorHeight;

	//FIXME: Very tight coupling with base class
	//FIXME: Peek decoder to latest I-frame on exccesive decoding lag
	//       ...but it's incompatible with intra-refresh

	switch (pkt.msg_case()) {
	case msg::Packet::kDesktopFrame:
		decoder->pushData(extraData, pkt.extra_data_len());
		/* lock */ {
			FrameData now;
			if (pkt.desktop_frame().cursor_visible()) {
				now.cursorX = pkt.desktop_frame().cursor_x();
				now.cursorY = pkt.desktop_frame().cursor_y();
			}
			else {
				now.cursorX = -1;
				now.cursorY = -1;
			}
			if (pendingCursorChange != nullptr) {
				now.cursorDataUpdate = std::move(pendingCursorChange);
				now.cursorWidth = cursorWidth;
				now.cursorHeight = cursorHeight;
				pendingCursorChange.reset();
			}
			std::lock_guard lock(frameDataLock);
			undecodedFrameData.push_back(std::move(now));
		}
		break;
	case msg::Packet::kCursorShape: {
		const auto& data = pkt.cursor_shape();
		pendingCursorChange = std::make_shared<ByteBuffer>(data.width() * data.height() * 4);
		memcpy(pendingCursorChange->data(), extraData, pkt.extra_data_len());
		cursorWidth = data.width();
		cursorHeight = data.height();
	}
		break;
	default:
		log->error("processNewPacket received unknown packet type: {}", pkt.msg_case());
	}
}

void StreamViewerD3D::_init() {
	HRESULT hr;

	hr = CreateDXGIFactory1(dxgiFactory.guid(), dxgiFactory.data());
	check_quit(FAILED(hr), log, "Failed to create dxgi factory");

	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

	hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		&featureLevel, 1, D3D11_SDK_VERSION, device.data(), nullptr, context.data());
	check_quit(FAILED(hr), log, "Failed to create D3D11 device");

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = 0;
	swapChainDesc.Height = 0;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	hr = dxgiFactory->CreateSwapChainForHwnd(device.ptr(), hwnd(), &swapChainDesc, nullptr, nullptr, swapChain.data());
	check_quit(FAILED(hr), log, "Failed to create swap chain");

	D3D11Texture2D framebuffer;
	hr = swapChain->GetBuffer(0, framebuffer.guid(), framebuffer.data());
	check_quit(FAILED(hr), log, "Failed to get framebuffer");

	hr = device->CreateRenderTargetView(framebuffer.ptr(), nullptr, framebufferRTV.data());
	check_quit(FAILED(hr), log, "Failed to create framebuffer RTV");

	std::vector<uint8_t> vertexBlobFull = loadFile(L"viewer-vs_full.fxc");
	hr = device->CreateVertexShader(vertexBlobFull.data(), vertexBlobFull.size(), nullptr, vertexShaderFull.data());
	check_quit(FAILED(hr), log, "Failed to create vertex shader (full)");

	std::vector<uint8_t> vertexBlobBox = loadFile(L"viewer-vs_box.fxc");
	hr = device->CreateVertexShader(vertexBlobBox.data(), vertexBlobBox.size(), nullptr, vertexShaderBox.data());
	check_quit(FAILED(hr), log, "Failed to create vertex shader (box)");

	std::vector<uint8_t> pixelBlob = loadFile(L"viewer-ps_main.fxc");
	hr = device->CreatePixelShader(pixelBlob.data(), pixelBlob.size(), nullptr, pixelShader.data());
	check_quit(FAILED(hr), log, "Failed to create pixel shader");

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

	D3D11_INPUT_ELEMENT_DESC inputLayoutDesc[] = {
		{"POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};
	device->CreateInputLayout(inputLayoutDesc, 1, vertexBlobFull.data(), vertexBlobFull.size(), inputLayoutFull.data());
	device->CreateInputLayout(inputLayoutDesc, 1, vertexBlobBox.data(), vertexBlobBox.size(), inputLayoutBox.data());

	D3D11_BUFFER_DESC vertexBufferDesc = {};
	vertexBufferDesc.ByteWidth = sizeof(quadVertex);
	vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA vertexBufferData = {};
	vertexBufferData.pSysMem = quadVertex;
	device->CreateBuffer(&vertexBufferDesc, &vertexBufferData, vertexBuffer.data());

	D3D11_BUFFER_DESC cbufferDesc = {};
	cbufferDesc.ByteWidth = sizeof(float) * 4;
	cbufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	device->CreateBuffer(&cbufferDesc, nullptr, cbuffer.data());

	D3D11_TEXTURE2D_DESC desktopTexDesc = {};
	desktopTexDesc.Width = width;
	desktopTexDesc.Height = height;
	desktopTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desktopTexDesc.ArraySize = 1;
	desktopTexDesc.MipLevels = 1;
	desktopTexDesc.SampleDesc.Count = 1;
	desktopTexDesc.Usage = D3D11_USAGE_DYNAMIC;
	desktopTexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desktopTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	hr = device->CreateTexture2D(&desktopTexDesc, nullptr, desktopTex.data());
	check_quit(FAILED(hr), log, "Failed to allocate desktop texture");

	D3D11_SHADER_RESOURCE_VIEW_DESC desktopSrvDesc = {};
	desktopSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	desktopSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desktopSrvDesc.Texture2D.MipLevels = 1;
	desktopSrvDesc.Texture2D.MostDetailedMip = 0;
	device->CreateShaderResourceView(desktopTex.ptr(), &desktopSrvDesc, desktopSRV.data());
	check_quit(FAILED(hr), log, "Failed to create desktop SRV");

	D3D11_BLEND_DESC blendStateDesc = {};
	blendStateDesc.RenderTarget[0].BlendEnable = true;
	blendStateDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendStateDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendStateDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendStateDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendStateDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendStateDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendStateDesc.RenderTarget[0].RenderTargetWriteMask = 0x0f;
	device->CreateBlendState(&blendStateDesc, blendState.data());

	cursorTexWidth = 128;
	cursorTexHeight = 128;
	_recreateCursorTexture();

	decoder = std::make_unique<DecoderSoftware>();
	decoder->setOnFrameAvailable([this](TextureSoftware&& frame) { _onNewFrame(std::move(frame)); });
	decoder->start();

	flagRunRender.store(true, std::memory_order_release);
	renderThread = std::thread([this]() { _renderLoop(); });
}

void StreamViewerD3D::_recreateCursorTexture() {
	HRESULT hr;

	cursorTex.release();
	cursorSRV.release();

	D3D11_TEXTURE2D_DESC cursorTexDesc = {};
	cursorTexDesc.Width = cursorTexWidth;
	cursorTexDesc.Height = cursorTexHeight;
	cursorTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	cursorTexDesc.ArraySize = 1;
	cursorTexDesc.MipLevels = 1;
	cursorTexDesc.SampleDesc.Count = 1;
	cursorTexDesc.Usage = D3D11_USAGE_DYNAMIC;
	cursorTexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cursorTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	hr = device->CreateTexture2D(&cursorTexDesc, nullptr, cursorTex.data());
	check_quit(FAILED(hr), log, "Failed to allocate cursor texture");

	D3D11_SHADER_RESOURCE_VIEW_DESC cursorSrvDesc = {};
	cursorSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	cursorSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	cursorSrvDesc.Texture2D.MipLevels = 1;
	cursorSrvDesc.Texture2D.MostDetailedMip = 0;
	hr = device->CreateShaderResourceView(cursorTex.ptr(), &cursorSrvDesc, cursorSRV.data());
	check_quit(FAILED(hr), log, "Failed to create cursor SRV");
}

void StreamViewerD3D::_onNewFrame(TextureSoftware&& frame) {
	std::lock_guard lock(frameDataLock);

	FrameData now = std::move(undecodedFrameData.front());
	undecodedFrameData.pop_front();

	now.desktop = std::move(frame);
	frameData.push_back(std::move(now));
}

void StreamViewerD3D::_renderLoop() {
	bool desktopLoaded = false;
	bool cursorLoaded = false;
	float clearColor[4] = { 0, 0, 0, 1 };

	int cursorX = -1, cursorY = -1;

	while (flagRunRender.load(std::memory_order_acquire)) {
		bool hasNewFrameData = false;
		FrameData nowFrameData;

		/* lock */ {
			std::lock_guard lock(frameDataLock);

			while (frameData.size() > 3)
				frameData.pop_front();

			if (!frameData.empty()) {
				nowFrameData = std::move(frameData.front());
				frameData.pop_front();
				hasNewFrameData = true;
			}
		}

		if (hasNewFrameData) {
			D3D11_MAPPED_SUBRESOURCE mapInfo;
			context->Map(desktopTex.ptr(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapInfo);

			uint8_t* dstPtr = reinterpret_cast<uint8_t*>(mapInfo.pData);
			uint8_t* srcPtr = nowFrameData.desktop.data[0];

			if (mapInfo.RowPitch != nowFrameData.desktop.linesize[0]) {
				for (int i = 0; i < height; i++)
					memcpy(dstPtr + (i * mapInfo.RowPitch), srcPtr + (i * nowFrameData.desktop.linesize[0]), width * 4);
			}
			else {
				// Fast path
				memcpy(dstPtr, srcPtr, height * mapInfo.RowPitch);
			}

			context->Unmap(desktopTex.ptr(), 0);
			desktopLoaded = true;

			if (nowFrameData.cursorX != cursorX || nowFrameData.cursorY != cursorY) {
				cursorX = nowFrameData.cursorX;
				cursorY = nowFrameData.cursorY;

				D3D11_MAPPED_SUBRESOURCE mapInfo = {};
				context->Map(cbuffer.ptr(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapInfo);
				float* pData = reinterpret_cast<float*>(mapInfo.pData);
				pData[0] = (float)cursorX / width;
				pData[1] = (float)cursorY / height;
				pData[2] = (float)cursorTexWidth / width;
				pData[3] = (float)cursorTexHeight / height;
				context->Unmap(cbuffer.ptr(), 0);
			}

			if (nowFrameData.cursorDataUpdate != nullptr) {
				cursorLoaded = true;

				if (cursorTexWidth < nowFrameData.cursorWidth || cursorTexHeight < nowFrameData.cursorHeight) {
					cursorTexWidth = cursorTexHeight = std::max(nowFrameData.cursorWidth, nowFrameData.cursorHeight);
					_recreateCursorTexture();
				}

				D3D11_MAPPED_SUBRESOURCE mapInfo = {};
				context->Map(cursorTex.ptr(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapInfo);
				uint8_t* pDst = reinterpret_cast<uint8_t*>(mapInfo.pData);
				uint8_t* pSrc = nowFrameData.cursorDataUpdate->data();
				memset(pDst, 0, cursorTexHeight * mapInfo.RowPitch);
				for (int i = 0; i < nowFrameData.cursorHeight; i++)
					memcpy(pDst + (i * mapInfo.RowPitch), pSrc + (i * nowFrameData.cursorWidth * 4), nowFrameData.cursorWidth * 4);
				context->Unmap(cursorTex.ptr(), 0);
			}
		}

		context->ClearRenderTargetView(framebufferRTV.ptr(), clearColor);
		context->OMSetRenderTargets(1, framebufferRTV.data(), nullptr);
		context->OMSetBlendState(blendState.ptr(), nullptr, 0xffffffff);

		if (desktopLoaded) {
			DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
			swapChain->GetDesc1(&swapChainDesc);

			D3D11_VIEWPORT viewport = {
				0, 0,
				(float)(swapChainDesc.Width), (float)(swapChainDesc.Height),
				0, 1
			};
			context->RSSetViewports(1, &viewport);

			context->VSSetShader(vertexShaderFull.ptr(), nullptr, 0);

			context->PSSetShader(pixelShader.ptr(), nullptr, 0);
			context->PSSetShaderResources(0, 1, desktopSRV.data());
			context->PSSetSamplers(0, 1, clampSampler.data());

			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			context->IASetInputLayout(inputLayoutFull.ptr());
			context->IASetVertexBuffers(0, 1, vertexBuffer.data(), &quadVertexStride, &quadVertexOffset);
			context->Draw(quadVertexCount, 0);

			if (cursorLoaded && cursorX >= 0) {
				context->VSSetShader(vertexShaderBox.ptr(), nullptr, 0);
				context->VSSetConstantBuffers(0, 1, cbuffer.data());

				context->PSSetShaderResources(0, 1, cursorSRV.data());

				context->IASetInputLayout(inputLayoutBox.ptr());
				context->IASetVertexBuffers(0, 1, vertexBuffer.data(), &quadVertexStride, &quadVertexOffset);
				context->Draw(quadVertexCount, 0);
			}
		}

		swapChain->Present(1, 0);
	}
}