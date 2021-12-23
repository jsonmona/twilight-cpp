#ifndef SERVER_CAPTURE_PIPELINE_H_
#define SERVER_CAPTURE_PIPELINE_H_

#include "common/ByteBuffer.h"
#include "common/DesktopFrame.h"
#include "common/StatisticMixer.h"

#include <packet.pb.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class CapturePipeline {
protected:
    std::function<void(DesktopFrame<ByteBuffer>&&)> writeOutput;

public:
    static std::unique_ptr<CapturePipeline> createInstance();

    CapturePipeline() = default;
    CapturePipeline(const CapturePipeline& copy) = delete;
    CapturePipeline(CapturePipeline&& move) = default;
    CapturePipeline& operator=(const CapturePipeline& copy) = delete;
    CapturePipeline& operator=(CapturePipeline&& move) = delete;

    virtual ~CapturePipeline(){};

    template <typename Fn>
    void setOutputCallback(Fn fn) {
        writeOutput = std::move(fn);
    }

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual StatisticMixer::Stat calcCaptureStat() = 0;
    virtual StatisticMixer::Stat calcEncoderStat() = 0;
};

#endif