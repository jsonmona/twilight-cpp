#include "DecoderFFmpeg.h"

TWILIGHT_DEFINE_LOGGER(DecoderFFmpeg);

DecoderFFmpeg::DecoderFFmpeg() : flagRun(false), flagKeyInPacket(false), codec(nullptr), avctx(nullptr) {}

DecoderFFmpeg::~DecoderFFmpeg() {
    bool wasRunning = flagRun.exchange(false, std::memory_order_relaxed);
    log.assert_quit(!wasRunning, "Being destructed while running!");

    if (runThread.joinable())
        runThread.join();

    avcodec_free_context(&avctx);
}

std::vector<CodecType> DecoderFFmpeg::enumSupportedCodecs() {
    std::vector<CodecType> ret;
    ret.reserve(1);

    ret.push_back(CodecType::VP8);
    return ret;
}

void DecoderFFmpeg::setVideoResolution(int width_, int height_) {
    log.assert_quit(!flagRun.load(std::memory_order_relaxed), "Tried to set resolution while running!");
}

void DecoderFFmpeg::init(CodecType codecType_, std::shared_ptr<NetworkClock> clock_) {
    codecType = codecType_;
    clock = std::move(clock_);

    switch (codecType) {
    case CodecType::VP8:
        codec = avcodec_find_decoder_by_name("vp8");
        log.assert_quit(codec != nullptr, "Failed to find ffvp8 decoder");
        break;
    default:
        log.error_quit("Unknown codec type ({}) requested!", (intmax_t)codecType);
    }
}

bool DecoderFFmpeg::readSoftware(DesktopFrame<TextureSoftware>* output) {
    std::unique_lock lock(frameLock);

    while (frameQueue.empty() && flagRun.load(std::memory_order_relaxed))
        frameCV.wait(lock);

    if (!flagRun.load(std::memory_order_relaxed))
        return false;

    // Keep a reference so that it does not get freed
    prevReadFrame = std::move(frameQueue.front().desktop);
    *output = frameQueue.front().getOtherType(TextureSoftware::reference(prevReadFrame->data, prevReadFrame->linesize,
                                                                         prevReadFrame->width, prevReadFrame->height,
                                                                         (AVPixelFormat)prevReadFrame->format));
    frameQueue.pop_front();
    frameCV.notify_one();

    return true;
}

void DecoderFFmpeg::start() {
    int err;
    log.assert_quit(!flagRun.load(std::memory_order_relaxed), "Not stopped before start!");
    log.assert_quit(codec != nullptr, "Codec not set before start!");

    if (runThread.joinable())
        runThread.join();

    flagKeyInPacket = false;

    avcodec_free_context(&avctx);

    avctx = avcodec_alloc_context3(codec);
    log.assert_quit(avctx != nullptr, "Failed to allocate codec context");

    avctx->thread_type = FF_THREAD_SLICE;
    avctx->thread_count = 4;
    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    AVDictionary* options = nullptr;

    err = avcodec_open2(avctx, codec, &options);
    log.assert_quit(err == 0, "Failed to open codec");

    if (av_dict_count(options) != 0) {
        log.error("Codec has rejected some options:");
        AVDictionaryEntry* entry = nullptr;
        while (true) {
            entry = av_dict_get(options, "", entry, AV_DICT_IGNORE_SUFFIX);
            if (entry == nullptr)
                break;
            log.error("    {} = {}", entry->key, entry->value);
        }
    }

    av_dict_free(&options);

    flagRun.store(true, std::memory_order_release);
    runThread = std::thread(&DecoderFFmpeg::run_, this);
}

void DecoderFFmpeg::stop() {
    bool wasRunning = flagRun.exchange(false, std::memory_order_relaxed);
    log.assert_quit(wasRunning, "Not started before stopping!");

    std::scoped_lock lock(packetLock, frameLock);
    packetCV.notify_all();
    frameCV.notify_all();
}

void DecoderFFmpeg::pushData(DesktopFrame<ByteBuffer>&& frame) {
    std::lock_guard lock(packetLock);
    if (frame.isIDR) {
        if (flagKeyInPacket) {
            // Two IDR packets in queue. Skip all remaining packets
            log.warn("Skipping {} frames! Is decoder overloaded?", packetQueue.size());

            std::shared_ptr<CursorPos> lastPos;
            std::shared_ptr<CursorShape> lastShape;
            for (auto& now : packetQueue) {
                if (now.cursorPos)
                    lastPos = std::move(now.cursorPos);
                if (now.cursorShape)
                    lastShape = std::move(now.cursorShape);
            }

            packetQueue.clear();

            if (lastPos && !frame.cursorPos)
                frame.cursorPos = std::move(lastPos);
            if (lastShape && !frame.cursorShape)
                frame.cursorShape = std::move(lastShape);
        }
        flagKeyInPacket = true;
    }
    packetQueue.push_back(std::move(frame));
    packetCV.notify_one();
}

void DecoderFFmpeg::run_() {
    int err;

    long long pts = 0;
    AVFramePtr fr;
    AVPacketPtr pkt;
    std::deque<DesktopFrame<long long>> extraDataList;

    while (flagRun.load(std::memory_order_acquire)) {
        err = avcodec_receive_frame(avctx, fr.get());
        if (err == AVERROR_EOF)
            break;
        else if (err == 0) {
            DesktopFrame<long long> extraData;
            extraData.desktop = -1;
            for (auto itr = extraDataList.begin(); itr != extraDataList.end(); ++itr) {
                if (itr->desktop == fr->pts) {
                    extraData = std::move(*itr);
                    extraDataList.erase(itr);
                    break;
                }
            }
            log.assert_quit(0 <= extraData.desktop, "Failed to find matching extra data for {}", fr->pts);

            DesktopFrame<AVFramePtr> output = extraData.getOtherType(std::move(fr));
            output.timeDecoded = clock->time();

            /* acquire lock */ {
                std::unique_lock lock(frameLock);
                while (frameQueue.full() && flagRun.load(std::memory_order_relaxed))
                    frameCV.wait(lock);
                if (!flagRun.load(std::memory_order_relaxed))
                    continue;
                frameQueue.push_back(std::move(output));
                frameCV.notify_one();
            }

            av_frame_unref(fr.get());
        } else if (err == AVERROR(EAGAIN)) {
            DesktopFrame<ByteBuffer> packet;
            /* acquire lock */ {
                std::unique_lock lock(packetLock);
                while (packetQueue.empty() && flagRun.load(std::memory_order_relaxed))
                    packetCV.wait(lock);
                if (!flagRun.load(std::memory_order_relaxed))
                    continue;
                packet = std::move(packetQueue.front());
                packetQueue.pop_front();
                if (packet.isIDR)
                    flagKeyInPacket = false;
            }

            pkt->data = packet.desktop.data();
            pkt->size = packet.desktop.size();
            pkt->pts = pts++;
            pkt->duration = 1;

            if (packet.isIDR)
                pkt->flags |= AV_PKT_FLAG_KEY;

            extraDataList.push_back(packet.getOtherType(std::move(pkt->pts)));

            err = avcodec_send_packet(avctx, pkt.get());
            log.assert_quit(err == 0, "Failed to send packet into decoder");

            av_packet_unref(pkt.get());
        } else {
            log.error_quit("Unknown error from encoder!");
        }
    }

    /* Drop all frames to save memory */ {
        std::lock_guard lock(frameLock);
        frameQueue.clear();
        prevReadFrame.free();
    }
}
