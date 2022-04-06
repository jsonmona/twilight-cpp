#include "EncoderFFmpeg.h"

#include <mutex>
#include <unordered_map>

EncoderFFmpeg::EncoderFFmpeg(LocalClock& clock)
    : log(createNamedLogger("EncoderFFmpeg")),
      clock(clock),
      flagRun(false),
      codecType(CodecType::VP8),
      width(-1),
      height(-1),
      codec(nullptr),
      avctx(nullptr) {
    codec = avcodec_find_encoder_by_name("libvpx");
    check_quit(codec == nullptr, log, "Failed to find libvpx encoder");
}

EncoderFFmpeg::~EncoderFFmpeg() {
    bool wasRunning = flagRun.exchange(false, std::memory_order_relaxed);
    check_quit(wasRunning, log, "Begin destructed while running!");

    if (runThread.joinable())
        runThread.join();
}

void EncoderFFmpeg::setMode(int width_, int height_, Rational framerate_) {
    width = width_;
    height = height_;
    framerate = framerate_;
}

void EncoderFFmpeg::start() {
    int err;
    check_quit(width <= 0 || height <= 0, log, "Not initialized before start!");
    check_quit(flagRun.load(std::memory_order_relaxed), log, "Not stopped before start!");

    if (runThread.joinable())
        runThread.join();

    flagNextFrameAvailable = false;
    flagNextPacketAvailable = false;

    avcodec_free_context(&avctx);

    avctx = avcodec_alloc_context3(codec);
    check_quit(avctx == nullptr, log, "Failed to allocate codec context");

    avctx->bit_rate = 7 * 1024 * 1024;
    avctx->rc_max_rate = 8 * 1024 * 1024;
    avctx->rc_min_rate = 500 * 1024;
    avctx->colorspace = AVCOL_SPC_BT709;
    avctx->color_range = AVCOL_RANGE_MPEG;
    avctx->thread_type = FF_THREAD_SLICE;
    avctx->thread_count = 4;
    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    avctx->width = width;
    avctx->height = height;
    avctx->gop_size = 120;

    // Time base is reciprocal of framerate
    avctx->time_base.num = framerate.den();
    avctx->time_base.den = framerate.num();

    AVDictionary* options = nullptr;
    err = av_dict_set(&options, "deadline", "good", 0);
    check_quit(err != 0, log, "Failed to set deadline=realtime");
    err = av_dict_set(&options, "cpu-used", "12", 0);
    check_quit(err != 0, log, "Failed to set cpu-used=12");
    err = av_dict_set(&options, "slices", "8", 0);
    check_quit(err != 0, log, "Failed to set slices=8");

    err = avcodec_open2(avctx, codec, &options);
    check_quit(err != 0, log, "Failed to open codec");

    if (av_dict_count(options) != 0) {
        log->error("Codec has rejected some options:");
        AVDictionaryEntry* entry = nullptr;
        while (true) {
            entry = av_dict_get(options, "", entry, AV_DICT_IGNORE_SUFFIX);
            if (entry == nullptr)
                break;
            log->error("    {} = {}", entry->key, entry->value);
        }
    }

    av_dict_free(&options);

    bool wasRunning = flagRun.exchange(true, std::memory_order_acq_rel);
    check_quit(wasRunning, log, "Trying to start when running!");

    if (runThread.joinable())
        runThread.join();
    runThread = std::thread(&EncoderFFmpeg::run_, this);
}

void EncoderFFmpeg::stop() {
    bool wasRunning = flagRun.exchange(false, std::memory_order_relaxed);
    check_quit(!wasRunning, log, "Trying to stop when not running!");

    frameCV.notify_all();
    packetCV.notify_all();
}

void EncoderFFmpeg::pushFrame(DesktopFrame<TextureSoftware>&& frame) {
    std::unique_lock lock(frameLock);
    while (flagNextFrameAvailable && flagRun.load(std::memory_order_relaxed))
        frameCV.wait(lock);
    if (!flagRun.load(std::memory_order_relaxed))
        return;
    nextFrame = std::move(frame);
    flagNextFrameAvailable = true;
    frameCV.notify_one();
}

bool EncoderFFmpeg::readData(DesktopFrame<ByteBuffer>* output) {
    std::unique_lock lock(packetLock);
    while (packetQueue.empty() && flagRun.load(std::memory_order_relaxed))
        packetCV.wait(lock);
    if (!flagRun.load(std::memory_order_relaxed))
        return false;
    *output = std::move(packetQueue.front());
    packetQueue.pop_front();
    return true;
}

void EncoderFFmpeg::run_() {
    using std::swap;
    int err;

    long long pts = 0;
    std::deque<DesktopFrame<long long>> extraDataList;

    AVFormatContext* fmt = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();

    err = avformat_alloc_output_context2(&fmt, nullptr, "matroska", nullptr);
    check_quit(err < 0, log, "Failed to allocate avformat context");

    AVStream* stream = avformat_new_stream(fmt, nullptr);
    check_quit(stream == nullptr, log, "Failed to mux new stream");

    stream->id = 0;
    stream->codecpar->codec_id = codec->id;
    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->color_primaries = avctx->color_primaries;
    stream->codecpar->color_range = avctx->color_range;
    stream->codecpar->color_space = avctx->colorspace;
    stream->codecpar->color_trc = avctx->color_trc;
    stream->codecpar->field_order = AV_FIELD_PROGRESSIVE;
    stream->codecpar->format = AV_PIX_FMT_YUV420P;
    stream->codecpar->height = height;
    stream->codecpar->width = width;
    stream->codecpar->level = avctx->level;
    stream->codecpar->profile = avctx->profile;
    stream->codecpar->sample_aspect_ratio.den = 1;
    stream->codecpar->sample_aspect_ratio.num = 1;
    stream->codecpar->video_delay = avctx->delay;
    stream->time_base.num = framerate.inv().num();
    stream->time_base.den = framerate.inv().den();

    err = avio_open(&fmt->pb, "dump.mkv", AVIO_FLAG_WRITE);
    check_quit(err < 0, log, "Failed to open output");

    AVDictionary* opt = nullptr;

    err = avformat_write_header(fmt, &opt);
    check_quit(err < 0, log, "Failed to write hedaer");

    av_dict_free(&opt);

    while (flagRun.load(std::memory_order_acquire)) {
        err = avcodec_receive_packet(avctx, pkt);
        if (err == AVERROR_EOF)
            break;
        else if (err == 0) {
            DesktopFrame<long long> extraData;
            extraData.desktop = -1;
            for (auto itr = extraDataList.begin(); itr != extraDataList.end(); ++itr) {
                if (itr->desktop == pkt->pts) {
                    extraData = std::move(*itr);
                    extraDataList.erase(itr);
                    break;
                }
            }
            check_quit(extraData.desktop == -1, log, "Failed to find matching extra data for {}", pkt->pts);

            ByteBuffer buf;
            buf.resize(pkt->size);
            buf.write(0, pkt->data, pkt->size);

            DesktopFrame<ByteBuffer> output = extraData.getOtherType(std::move(buf));
            output.isIDR = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
            output.timeEncoded = clock.time();

            AVRational originalFramerate;
            originalFramerate.num = framerate.inv().num();
            originalFramerate.den = framerate.inv().den();
            av_packet_rescale_ts(pkt, originalFramerate, stream->time_base);
            pkt->stream_index = 0;
            err = av_interleaved_write_frame(fmt, pkt);
            check_quit(err < 0, log, "Failed to write frame to file");

            av_packet_unref(pkt);

            std::lock_guard lock(packetLock);
            packetQueue.push_back(std::move(output));
            packetCV.notify_one();
        } else if (err == AVERROR(EAGAIN)) {
            DesktopFrame<TextureSoftware> frame;
            /* acquire lock */ {
                std::unique_lock lock(frameLock);
                while (!flagNextFrameAvailable && flagRun.load(std::memory_order_relaxed))
                    frameCV.wait(lock);
                if (!flagRun.load(std::memory_order_relaxed))
                    continue;
                frame = std::move(nextFrame);
                flagNextFrameAvailable = false;
                frameCV.notify_one();
            }
            check_quit(frame.desktop.width != width, log, "Frame size does not match configuration!");
            check_quit(frame.desktop.height != height, log, "Frame size does not match configuration!");
            check_quit(frame.desktop.format != avctx->pix_fmt, log, "Frame pix_fmt does not match configuration!");

            fr->format = (int)frame.desktop.format;
            fr->width = width;
            fr->height = height;
            fr->colorspace = AVCOL_SPC_BT709;
            fr->color_range = AVCOL_RANGE_MPEG;
            fr->pts = pts++;
            std::copy(frame.desktop.linesize, frame.desktop.linesize + 4, fr->linesize);
            if (codec->capabilities & AV_CODEC_CAP_DR1) {
                int linesize_align[AV_NUM_DATA_POINTERS] = {};
                avcodec_align_dimensions2(avctx, &fr->width, &fr->height, linesize_align);
                fr->crop_bottom = fr->height - height;
                fr->crop_right = fr->width - width;
                for (int i = 0; i < AV_NUM_DATA_POINTERS && fr->linesize[i] != 0; i++) {
                    if (fr->linesize[i] % linesize_align[i] != 0)
                        fr->linesize[i] += linesize_align[i] - fr->linesize[i] % linesize_align[i];
                }
            }
            int bufferSize = av_image_get_buffer_size(frame.desktop.format, fr->width, fr->height, 64) + 64;
            check_quit(bufferSize < 0, log, "Failed to get image buffer size");

            fr->buf[0] = av_buffer_alloc(bufferSize);
            err =
                av_image_fill_pointers(fr->data, (AVPixelFormat)fr->format, fr->height, fr->buf[0]->data, fr->linesize);
            check_quit(err < 0, log, "Failed to fill pointers for image");
            check_quit(bufferSize < err, log, "Buffer size calculated is not bug enough for image!");

            av_image_copy(fr->data, fr->linesize, const_cast<const uint8_t**>(frame.desktop.data),
                          frame.desktop.linesize, frame.desktop.format, frame.desktop.width, frame.desktop.height);

            extraDataList.push_back(frame.getOtherType(std::move(fr->pts)));

            err = avcodec_send_frame(avctx, fr);
            av_frame_unref(fr);

            if (err != 0)
                break;
        } else {
            error_quit(log, "Unknown error from encoder!");
        }
    }

    err = av_write_trailer(fmt);
    check_quit(err < 0, log, "Failed to write trailer");

    err = avio_closep(&fmt->pb);
    check_quit(err < 0, log, "Failed to close avio context");

    avformat_free_context(fmt);

    av_packet_free(&pkt);
    av_frame_free(&fr);
}
