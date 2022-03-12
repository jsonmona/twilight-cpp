#include "CapturePipelineFactory.h"

#include "server/platform/windows/CapturePipelineFactroryWin32.h"

std::unique_ptr<CapturePipelineFactory> CapturePipelineFactory::createInstance() {
    return std::make_unique<CapturePipelineFactoryWin32>();
}
