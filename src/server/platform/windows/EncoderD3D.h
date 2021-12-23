#ifndef SERVER_PLATFORM_WINDOWS_ENCODER_D3D_H_
#define SERVER_PLATFORM_WINDOWS_ENCODER_D3D_H_

#include "common/ByteBuffer.h"
#include "common/DesktopFrame.h"
#include "common/StatisticMixer.h"
#include "common/log.h"

#include "common/platform/windows/DeviceManagerD3D.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

class EncoderD3D {
    struct SideData {
        long long pts;
        std::chrono::steady_clock::time_point inputTime;
    };

    LoggerPtr log;

    int width, height;
    long long frameCnt;

    bool waitingInput;

    std::function<void(DesktopFrame<ByteBuffer>&&)> onDataAvailable;

    std::deque<DesktopFrame<SideData>> extraData;

    MFDxgiDeviceManager mfDeviceManager;
    MFTransform encoder;
    MFMediaEventGenerator eventGen;
    DWORD inputStreamId, outputStreamId;
    StatisticMixer statMixer;

    void init_();

    void pushEncoderTexture_(const D3D11Texture2D& tex, long long sampleDur, long long sampleTime);
    ByteBuffer popEncoderData_(long long* sampleTime);

public:
    EncoderD3D(DeviceManagerD3D _devs, int _width, int _height);
    ~EncoderD3D();

    template <typename Fn>
    void setOnDataAvailable(Fn fn) {
        onDataAvailable = std::move(fn);
    }

    void start();
    void stop();

    void poll();

    StatisticMixer::Stat calcEncoderStat() { return statMixer.calcStat(); }

    void pushFrame(DesktopFrame<D3D11Texture2D>&& cap);
};

#endif