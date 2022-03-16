#include "CapturePipelineFactroryWin32.h"

#include "server/platform/windows/CapturePipelineD3DMF.h"
#include "server/platform/windows/CapturePipelineD3DSoft.h"

CapturePipelineFactoryWin32::CapturePipelineFactoryWin32() : log(createNamedLogger("CapturePipelineFactoryWin32")) {}

CapturePipelineFactoryWin32::~CapturePipelineFactoryWin32() {}

std::vector<std::string> CapturePipelineFactoryWin32::listCapture() {
    // TODO: Make this sync with createPipeline better
    std::vector<std::string> ret;
    ret.push_back("DirectX 11");
    return ret;
}

std::vector<std::string> CapturePipelineFactoryWin32::listEncoder() {
    std::vector<std::string> ret;
    ret.push_back("Software (OpenH264) (slowest)");
    ret.push_back("Media Foundation");
    return ret;
}

std::pair<size_t, size_t> CapturePipelineFactoryWin32::getBestOption() {
    auto outputs = dxgiHelper.findAllOutput();
    if (outputs.empty())
        return {0, 0};

    D3D11Device dev = dxgiHelper.createDevice(dxgiHelper.getAdapterFromOutput(outputs[0]), true);
    if (dev.isInvalid())
        return {0, 0};

    return {0, 1};
}

std::pair<size_t, size_t> CapturePipelineFactoryWin32::getFallbackOption() {
    return {0, 0};
}

std::unique_ptr<CapturePipeline> CapturePipelineFactoryWin32::createPipeline(LocalClock& clock, size_t captureIdx,
                                                                             size_t encoderIdx) {
    switch (captureIdx) {
    case 0:
        break;
    default:
        error_quit(log, "Invalid captureIdx {}", captureIdx);
    }

    switch (encoderIdx) {
    case 0:
    case 1:
        break;
    default:
        error_quit(log, "Invalid encoderIdx {}", captureIdx);
    }

    if (encoderIdx == 0)
        return std::make_unique<CapturePipelineD3DSoft>(clock, dxgiHelper);
    if (encoderIdx == 1)
        return std::make_unique<CapturePipelineD3DMF>(clock, dxgiHelper);

    return nullptr;
}
