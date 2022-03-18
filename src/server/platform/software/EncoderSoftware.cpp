#include "EncoderSoftware.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <thread>

using namespace std::chrono_literals;

EncoderSoftware::EncoderSoftware(LocalClock& clock)
    : log(createNamedLogger("EncoderSoftware")),
      clock(clock),
      width(-1),
      height(-1),
      nextFrameAvailable(false),
      flagRun(false) {}

EncoderSoftware::~EncoderSoftware() {
    /* acquire data lock */ {
        std::lock_guard lock(dataLock);
        dataCV.notify_all();
    }

    if (runThread.joinable())
        runThread.join();
}

void EncoderSoftware::start() {
    check_quit(flagRun.load(std::memory_order_acquire), log, "Encoder is already started");
    if (runThread.joinable())
        runThread.join();

    flagRun.store(true, std::memory_order_relaxed);
    runThread = std::thread([this]() { run_(); });
}

void EncoderSoftware::stop() {
    flagRun.store(false, std::memory_order_release);
    dataCV.notify_all();
}

void EncoderSoftware::setResolution(int width, int height) {
    this->width = width;
    this->height = height;
}

void EncoderSoftware::run_() {
    int err;

    if (loader == nullptr) {
        loader = OpenH264Loader::getInstance();
        loader->prepare();
        check_quit(!loader->isReady(), log, "Not ready");
    }

    ISVCEncoder* encoder = nullptr;
    err = loader->CreateSVCEncoder(&encoder);
    check_quit(err != 0, log, "Failed to create encoder instance!");

    // TODO: Configure encoder not to skip frame
    SEncParamBase paramBase = {};
    paramBase.iUsageType = SCREEN_CONTENT_REAL_TIME;
    paramBase.fMaxFrameRate = 60;  // TODO: Determine FPS on runtime
    paramBase.iPicWidth = width;
    paramBase.iPicHeight = height;
    paramBase.iTargetBitrate = 7 * 1000 * 1000;
    paramBase.iRCMode = RC_BITRATE_MODE;

    err = encoder->Initialize(&paramBase);
    check_quit(err != 0, log, "Failed to initialize encoder");

    int videoFormat = videoFormatI420;
    err = encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &videoFormat);
    check_quit(err != 0, log, "Failed to set dataformat to {}", videoFormat);

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

        check_quit(pic.pData[1] != pic.pData[0] + (width * height), log, "Requirement #1 unsatisfied");
        check_quit(pic.pData[2] != pic.pData[1] + (width * height / 4), log, "Requirement #2 unsatisfied");

        err = encoder->EncodeFrame(&pic, &info);
        check_quit(err != 0, log, "Failed to encode a frame");

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
            onDataAvailable(cap.getOtherType(std::move(combined)));
        } else {
            // FIXME: What happens to sidedata when the frame is skipped?
            log->warn("Encoder decided to skip a frame");
        }
    }

    err = encoder->Uninitialize();
    if (err != 0)
        log->error("Failed to uninitialize encoder");

    loader->DestroySVCEncoder(encoder);
}

void EncoderSoftware::pushData(DesktopFrame<TextureSoftware>&& newData) {
    std::unique_lock lock(dataLock);
    while (nextFrameAvailable && flagRun.load(std::memory_order_acquire))
        dataCV.wait(lock);

    nextFrameAvailable = true;
    nextFrame = std::move(newData);
    dataCV.notify_one();
}
