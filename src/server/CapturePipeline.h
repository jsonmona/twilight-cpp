#ifndef TWILIGHT_SERVER_CAPTUREPIPELINE_H
#define TWILIGHT_SERVER_CAPTUREPIPELINE_H

#include "common/ByteBuffer.h"
#include "common/DesktopFrame.h"
#include "common/Rational.h"

#include <functional>
#include <string>
#include <vector>

class CapturePipeline {
public:
    CapturePipeline() = default;
    CapturePipeline(const CapturePipeline& copy) = delete;
    CapturePipeline(CapturePipeline&& move) = delete;

    virtual ~CapturePipeline() = default;

    template <typename Fn>
    void setOutputCallback(Fn fn) {
        writeOutput = std::move(fn);
    }

    virtual bool init() = 0;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void getNativeMode(int* width, int* height, Rational* framerate) = 0;

    virtual bool setCaptureMode(int width, int height, Rational framerate) = 0;
    virtual bool setEncoderMode(int width, int height, Rational framerate) = 0;

protected:
    std::function<void(DesktopFrame<ByteBuffer>&&)> writeOutput;
};

#endif