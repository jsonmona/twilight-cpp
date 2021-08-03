#ifndef SERVER_PLATFORM_WINDOWS_CAPTURE_PIPELINE_D3D_SOFT_H_
#define SERVER_PLATFORM_WINDOWS_CAPTURE_PIPELINE_D3D_SOFT_H_


#include "common/log.h"

#include "server/CapturePipeline.h"
#include "DeviceManagerD3D.h"
#include "CaptureD3D.h"

#include "common/platform/software/ScaleSoftware.h"
#include "server/platform/software/EncoderSoftware.h"

#include <atomic>
#include <thread>


class CapturePipelineD3DSoft : public CapturePipeline {
	LoggerPtr log;

	DeviceManagerD3D devs;
	CaptureD3D capture;
	ScaleSoftware scale;
	EncoderSoftware encoder;

	std::chrono::steady_clock::time_point lastPresentTime;

	std::atomic<bool> flagRun;
	std::thread runThread;
	
	CaptureData<TextureSoftware> _fetchTexture();

public:
	CapturePipelineD3DSoft(DeviceManagerD3D _devs, int w, int h, ScaleType type);
	~CapturePipelineD3DSoft() override;

	void start() override;
	void stop() override;
};


#endif