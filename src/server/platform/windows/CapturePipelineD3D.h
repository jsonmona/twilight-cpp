#ifndef SERVER_PLATFORM_WINDOWS_CAPTURE_PIPELINE_D3D_H_
#define SERVER_PLATFORM_WINDOWS_CAPTURE_PIPELINE_D3D_H_


#include "common/log.h"

#include "common/platform/windows/DeviceManagerD3D.h"

#include "server/CapturePipeline.h"
#include "CaptureD3D.h"
#include "ScaleD3D.h"
#include "EncoderD3D.h"

class CapturePipelineD3D : public CapturePipeline {
	LoggerPtr log;

	CaptureD3D capture;
	std::unique_ptr<ScaleD3D> scale;
	EncoderD3D encoder;

	std::thread runThread;
	std::atomic<bool> flagRun;

	void captureNextFrame_(DesktopFrame<D3D11Texture2D>&& cap);
	void run_();

public:
	CapturePipelineD3D(DeviceManagerD3D _devs, int w, int h, ScaleType type);
	~CapturePipelineD3D() override;

	void start() override;
	void stop() override;

	StatisticMixer::Stat calcCaptureStat() override { return capture.calcCaptureStat(); }
	StatisticMixer::Stat calcEncoderStat() override { return encoder.calcEncoderStat(); }
};


#endif