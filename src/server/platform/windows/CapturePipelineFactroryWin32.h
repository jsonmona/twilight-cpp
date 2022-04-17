#ifndef TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTUREPIPELINEFACTRORYWIN32_H
#define TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTUREPIPELINEFACTRORYWIN32_H

#include "common/log.h"

#include "common/platform/windows/DxgiHelper.h"

#include "server/CapturePipelineFactory.h"

class CapturePipelineFactoryWin32 : public CapturePipelineFactory {
public:
    CapturePipelineFactoryWin32();
    virtual ~CapturePipelineFactoryWin32();

    std::vector<std::string> listCapture() override;
    std::vector<std::string> listEncoder() override;

    std::pair<size_t, size_t> getBestOption() override;
    std::pair<size_t, size_t> getFallbackOption() override;

    std::unique_ptr<CapturePipeline> createPipeline(LocalClock& clock, size_t captureIdx, size_t encoderIdx) override;

private:
    static NamedLogger log;

    DxgiHelper dxgiHelper;
};

#endif
