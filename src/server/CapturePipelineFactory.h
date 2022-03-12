#ifndef TWILIGHT_SERVER_CAPTUREPIPELINEFACTORY_H
#define TWILIGHT_SERVER_CAPTUREPIPELINEFACTORY_H

#include <memory>
#include <string>
#include <vector>

class CapturePipeline;

class CapturePipelineFactory {
public:
    CapturePipelineFactory() = default;
    CapturePipelineFactory(const CapturePipelineFactory& copy) = delete;
    CapturePipelineFactory(CapturePipelineFactory&& move) = delete;

    virtual ~CapturePipelineFactory() = default;

    virtual std::vector<std::string> listCapture() = 0;
    virtual std::vector<std::string> listEncoder() = 0;

    virtual std::pair<size_t, size_t> getBestOption() = 0;
    virtual std::pair<size_t, size_t> getFallbackOption() = 0;

    virtual std::unique_ptr<CapturePipeline> createPipeline(size_t captureIdx, size_t encoderIdx) = 0;

    static std::unique_ptr<CapturePipelineFactory> createInstance();
};

#endif