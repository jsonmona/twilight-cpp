#ifndef SERVER_PLATFORM_WINDOWS_CAPTURE_PIPELINE_D3D_H_
#define SERVER_PLATFORM_WINDOWS_CAPTURE_PIPELINE_D3D_H_


#include "common/log.h"

#include "server/CapturePipeline.h"
#include "DeviceManagerD3D.h"
#include "CaptureD3D.h"
#include "ScaleD3D.h"
#include "EncoderD3D.h"

class CapturePipelineD3D : public CapturePipeline {
	LoggerPtr log;

	DeviceManagerD3D devs;
	CaptureD3D capture;
	std::unique_ptr<ScaleD3D> scale;
	EncoderD3D encoder;

	std::shared_ptr<CursorData> lastCursor;
	std::shared_ptr<CursorShapeData> lastCursorShape;

	std::atomic<bool> flagRun;

	std::thread runThread;

	CaptureData<D3D11Texture2D> _fetchTexture();
	void run_();

public:
	CapturePipelineD3D(DeviceManagerD3D _devs, int w, int h, ScaleType type);
	~CapturePipelineD3D() override;

	void start() override;
	void stop() override;
};


#endif