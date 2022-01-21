#ifndef TWILIGHT_SERVER_CAPTUREPIPELINE_H
#define TWILIGHT_SERVER_CAPTUREPIPELINE_H

#include "common/ByteBuffer.h"
#include "common/DesktopFrame.h"
#include "common/Rational.h"
#include "common/StatisticMixer.h"

#include <packet.pb.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
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

    virtual void getNativeMode(int* width, int* height, Rational* framerate) = 0;

    virtual void setMode(int width, int height, Rational framerate) = 0;

    virtual StatisticMixer::Stat calcCaptureStat() = 0;
    virtual StatisticMixer::Stat calcEncoderStat() = 0;
};

#endif