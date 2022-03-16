#ifndef TWILIGHT_SERVER_PLATFORM_WINDOWS_ENCODERMF_H
#define TWILIGHT_SERVER_PLATFORM_WINDOWS_ENCODERMF_H

#include "common/ByteBuffer.h"
#include "common/DesktopFrame.h"
#include "common/StatisticMixer.h"
#include "common/log.h"

#include "common/platform/windows/DxgiHelper.h"

#include "server/LocalClock.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

class EncoderMF {
public:
    explicit EncoderMF(LocalClock& clock);
    ~EncoderMF();

    template <typename Fn>
    void setOnDataAvailable(Fn fn) {
        onDataAvailable = std::move(fn);
    }

    void init(DxgiHelper dxgiHelper);
    void open(D3D11Device device, D3D11DeviceContext context);

    void start();
    void stop();

    void poll();

    bool pushFrame(DesktopFrame<D3D11Texture2D>* cap);

private:
    LoggerPtr log;

    LocalClock& clock;

    int width, height;
    long long frameCnt;

    bool waitingInput;
    bool initialized;

    UINT resetToken;

    std::function<void(DesktopFrame<ByteBuffer>&&)> onDataAvailable;

    std::deque<DesktopFrame<long long>> extraData;

    MFDxgiDeviceManager mfDeviceManager;
    MFTransform encoder;
    MFMediaEventGenerator eventGen;
    DWORD inputStreamId, outputStreamId;

    void init_();

    void pushEncoderTexture_(const D3D11Texture2D& tex, long long sampleDur, long long sampleTime);
    ByteBuffer popEncoderData_(long long* sampleTime);
};

#endif