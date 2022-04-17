#include "EncoderOpenH264.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <thread>

using namespace std::chrono_literals;

TWILIGHT_DEFINE_LOGGER(EncoderOpenH264);

EncoderOpenH264::EncoderOpenH264(LocalClock& clock)
    : clock(clock), width(-1), height(-1), nextFrameAvailable(false), flagRun(false) {}

EncoderOpenH264::~EncoderOpenH264() {
    /* acquire data lock */ {
        std::lock_guard lock(dataLock);
        dataCV.notify_all();
    }

    if (runThread.joinable())
        runThread.join();
}

void EncoderOpenH264::start() {
    log.assert_quit(!flagRun.load(std::memory_order_acquire), "Encoder is already started");
    if (runThread.joinable())
        runThread.join();

    flagRun.store(true, std::memory_order_relaxed);
    runThread = std::thread([this]() { run_(); });
}

void EncoderOpenH264::stop() {
    flagRun.store(false, std::memory_order_release);
    dataCV.notify_all();
}

void EncoderOpenH264::setResolution(int width, int height) {
    this->width = width;
    this->height = height;
}

void EncoderOpenH264::run_() {
    int err;

    if (loader == nullptr) {
        loader = OpenH264Loader::getInstance();
        loader->prepare();
        log.assert_quit(loader->isReady(), "OpenH264 not ready after prepare()!");
    }

    ISVCEncoder* encoder = nullptr;
    err = loader->CreateSVCEncoder(&encoder);
    log.assert_quit(err == 0, "Failde to create encoder instance");

    // TODO: Configure encoder not to skip frame
    SEncParamBase paramBase = {};
    paramBase.iUsageType = SCREEN_CONTENT_REAL_TIME;
    paramBase.fMaxFrameRate = 60;  // TODO: Determine FPS on runtime
    paramBase.iPicWidth = width;
    paramBase.iPicHeight = height;
    paramBase.iTargetBitrate = 7 * 1000 * 1000;
    paramBase.iRCMode = RC_BITRATE_MODE;

    err = encoder->Initialize(&paramBase);
    log.assert_quit(err == 0, "Failed to initialize encoder");

    int videoFormat = videoFormatI420;
    err = encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &videoFormat);
    log.assert_quit(err == 0, "Failed to set dataformat to {}", videoFormat);

    while (flagRun.load(std::memory_order_acquire)) {
        DesktopFrame<TextureSoftware> cap;

        /* lock */ {
            std::unique_lock lock(dataLock);
            while (!nextFrameAvailable && flagRun.load(std::memory_order_relaxed))
                dataCV.wait(lock);

            if (!flagRun.load(std::memory_order_relaxed))
                continue;

            cap = std::move(nextFrame);
            nextFrameAvailable = false;
            dataCV.notify_one();
        }

        SFrameBSInfo info = {};
        SSourcePicture pic = {};
        pic.iPicWidth = cap.desktop.width;
        pic.iPicHeight = cap.desktop.height;
        pic.iColorFormat = videoFormatI420;
        pic.iStride[0] = cap.desktop.linesize[0];
        pic.iStride[1] = cap.desktop.linesize[1];
        pic.iStride[2] = cap.desktop.linesize[2];
        pic.pData[0] = cap.desktop.data[0];
        pic.pData[1] = cap.desktop.data[1];
        pic.pData[2] = cap.desktop.data[2];

        log.assert_quit(pic.pData[1] == pic.pData[0] + (width * height), "Requirement #1 unsatisfied");
        log.assert_quit(pic.pData[2] == pic.pData[1] + (width * height / 4), "Requirement #2 unsatisfied");

        err = encoder->EncodeFrame(&pic, &info);
        log.assert_quit(err == 0, "Failed to encode a frame");

        if (info.eFrameType != videoFrameTypeSkip) {
            ByteBuffer combined;
            combined.reserve(info.iFrameSizeInBytes);

            for (int i = 0; i < info.iLayerNum; i++) {
                auto& layerInfo = info.sLayerInfo[i];

                int layerSize = 0;
                for (int j = 0; j < layerInfo.iNalCount; j++)
                    layerSize += layerInfo.pNalLengthInByte[j];

                combined.append(layerInfo.pBsBuf, layerSize);
            }

            cap.timeEncoded = clock.time();
            cap.isIDR = info.eFrameType == videoFrameTypeIDR;
            onDataAvailable(cap.getOtherType(std::move(combined)));
        } else {
            // FIXME: What happens to sidedata when the frame is skipped?
            log.warn("Encoder decided to skip a frame");
        }
    }

    err = encoder->Uninitialize();
    if (err != 0)
        log.error("Failed to uninitialize encoder");

    loader->DestroySVCEncoder(encoder);
}

void EncoderOpenH264::pushData(DesktopFrame<TextureSoftware>&& newData) {
    std::unique_lock lock(dataLock);
    while (nextFrameAvailable && flagRun.load(std::memory_order_acquire))
        dataCV.wait(lock);

    nextFrameAvailable = true;
    nextFrame = std::move(newData);
    dataCV.notify_one();
}
