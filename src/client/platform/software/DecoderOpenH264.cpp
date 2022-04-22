#include "DecoderOpenH264.h"

#include <map>

TWILIGHT_DEFINE_LOGGER(DecoderOpenH264);

DecoderOpenH264::DecoderOpenH264() : flagRun(false), width(-1), height(-1) {}

DecoderOpenH264::~DecoderOpenH264() {
    log.assert_quit(!flagRun.load(std::memory_order_relaxed), "Being destructed without stopping");

    if (runThread.joinable())
        runThread.join();
}

std::vector<CodecType> DecoderOpenH264::enumSupportedCodecs() {
    std::vector<CodecType> ret;
    ret.reserve(1);

    ret.push_back(CodecType::H264_BASELINE);
    return ret;
}

void DecoderOpenH264::setVideoResolution(int width_, int height_) {
    log.assert_quit(!flagRun.load(std::memory_order_relaxed), "Tried to set resolution while running!");

    width = width_;
    height = height_;
}

void DecoderOpenH264::init(CodecType codecType, std::shared_ptr<NetworkClock> clock_) {
    log.assert_quit(codecType == CodecType::H264_BASELINE, "Unsupported codec {} was requested!", (int)codecType);
    clock = std::move(clock_);
}

void DecoderOpenH264::start() {
    log.assert_quit(clock != nullptr, "Not initialized before start!");

    if (loader == nullptr) {
        loader = OpenH264Loader::getInstance();
        loader->prepare();
    }

    flagRun.store(true, std::memory_order_release);
    runThread = std::thread([this]() { run_(); });
}

void DecoderOpenH264::stop() {
    flagRun.store(false, std::memory_order_release);

    std::scoped_lock lock(packetLock, frameLock);
    packetCV.notify_all();
    frameCV.notify_all();
}

void DecoderOpenH264::pushData(DesktopFrame<ByteBuffer> &&nextData) {
    std::lock_guard lock(packetLock);
    packetQueue.push_back(std::move(nextData));
    packetCV.notify_one();
}

bool DecoderOpenH264::readSoftware(DesktopFrame<TextureSoftware> *output) {
    std::unique_lock lock(frameLock);
    while (frameQueue.empty() && flagRun.load(std::memory_order_relaxed))
        frameCV.wait(lock);

    if (!flagRun.load(std::memory_order_relaxed))
        return false;

    *output = std::move(frameQueue.front());
    frameQueue.pop_front();
    return true;
}

void DecoderOpenH264::run_() {
    int err;
    ISVCDecoder *decoder;

    err = loader->CreateDecoder(&decoder);
    log.assert_quit(err == 0 && decoder != nullptr, "Failed to create decoder instance");

    std::shared_ptr<TextureAllocArena> arena;

    SDecodingParam decParam = {};
    decParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    decParam.bParseOnly = false;

    err = decoder->Initialize(&decParam);
    log.assert_quit(err == 0, "Failed to initialize decoder");

    while (flagRun.load(std::memory_order_acquire)) {
        DesktopFrame<ByteBuffer> data;

        /* lock_guard */ {
            std::unique_lock lock(packetLock);

            while (packetQueue.empty() && flagRun.load(std::memory_order_relaxed))
                packetCV.wait(lock);

            if (!flagRun.load(std::memory_order_relaxed))
                break;

            data = std::move(packetQueue.front());
            packetQueue.pop_front();
        }

        uint8_t *framebuffer[3] = {};
        SBufferInfo decBufferInfo = {};
        err = decoder->DecodeFrameNoDelay(data.desktop.data(), data.desktop.size(), framebuffer, &decBufferInfo);
        log.assert_quit(err == 0, "Failed to decode frame");

        if (decBufferInfo.iBufferStatus == 1) {
            int w = decBufferInfo.UsrData.sSystemBuffer.iWidth;
            int h = decBufferInfo.UsrData.sSystemBuffer.iHeight;

            EVideoFormatType openH264Format = (EVideoFormatType)decBufferInfo.UsrData.sSystemBuffer.iFormat;
            AVPixelFormat fmt = openH264Format == videoFormatI420 ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_NONE;
            log.assert_quit(fmt != AV_PIX_FMT_NONE, "Unknown OpenH264 video format {}",
                            decBufferInfo.UsrData.sSystemBuffer.iFormat);

            int linesize[4] = {decBufferInfo.UsrData.sSystemBuffer.iStride[0],
                               decBufferInfo.UsrData.sSystemBuffer.iStride[1],
                               decBufferInfo.UsrData.sSystemBuffer.iStride[1], 0};
            if (framebuffer[0] != decBufferInfo.pDst[0] || framebuffer[1] != decBufferInfo.pDst[1] ||
                framebuffer[2] != decBufferInfo.pDst[2]) {
                log.info("linesize: {} {} {}", linesize[0], linesize[1], linesize[2]);
                log.info("framebuffer: {} {} {}", (uintptr_t)framebuffer[0], (uintptr_t)framebuffer[1],
                         (uintptr_t)framebuffer[2]);
                log.info("bufferinfo: {} {} {}", (uintptr_t)decBufferInfo.pDst[0], (uintptr_t)decBufferInfo.pDst[1],
                         (uintptr_t)decBufferInfo.pDst[2]);
            }

            if (arena == nullptr) {
                arena = TextureAllocArena::getArena(w, h, fmt);
            } else {
                log.assert_quit(arena->checkConfig(w, h, fmt), "Texture format changed while running!");
            }

            auto frame = data.getOtherType(TextureSoftware::reference(framebuffer, linesize, w, h, fmt).clone(arena));
            frame.timeDecoded = clock->time();
            std::lock_guard lock(frameLock);
            frameQueue.push_back(std::move(frame));
        } else
            log.info("No frame provided");
    }

    err = decoder->Uninitialize();
    if (err != 0)
        log.warn("Failed to uninitialize decoder");
    loader->DestroyDecoder(decoder);

    /* Deallocate all texture before destructing alloc arena */ {
        std::lock_guard lock(frameLock);
        frameQueue.clear();
    }
}
