#ifndef SERVER_PLATFORM_WINDOWS_CAPTURE_PIPELINE_D3D_SOFT_H_
#define SERVER_PLATFORM_WINDOWS_CAPTURE_PIPELINE_D3D_SOFT_H_


#include "common/log.h"

#include "server/CapturePipeline.h"
#include "DeviceManagerD3D.h"
#include "CaptureD3D.h"

#include "server/platform/software/ScaleSoftware.h"
#include "server/platform/software/EncoderSoftware.h"

class CapturePipelineD3DSoft : public CapturePipeline {
	DeviceManagerD3D devs;
	CaptureD3D capture;
	ScaleSoftware scale;
	EncoderSoftware encoder;

	CaptureData<TextureSoftware> _fetchTexture();

public:
	CapturePipelineD3DSoft(DeviceManagerD3D _devs, int w, int h, ScaleType type);
	~CapturePipelineD3DSoft() override;

	void start() override;
	void stop() override;
};


#endif