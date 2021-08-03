#include "CapturePipeline.h"

#include "common/platform/windows/DeviceManagerD3D.h"

#include "server/platform/windows/CapturePipelineD3D.h"
#include "server/platform/windows/CapturePipelineD3DSoft.h"


std::unique_ptr<CapturePipeline> CapturePipeline::createInstance() {
	std::unique_ptr<CapturePipeline> ptr;

	DeviceManagerD3D devs;
	if (devs.isVideoSupported())
		ptr = std::make_unique<CapturePipelineD3D>(devs, 1920, 1080, ScaleType::NV12);
	else
		ptr = std::make_unique<CapturePipelineD3DSoft>(devs, 1920, 1080, ScaleType::NV12);

	return ptr;
}
