#include "AudioEncoder.h"

TWILIGHT_DEFINE_LOGGER(AudioEncoder);

AudioEncoder::AudioEncoder() : cap(std::make_unique<AudioCaptureWASAPI>()) {
    cap->setOnConfigured([this](AVSampleFormat fmt, int sr, int ch) {
        samplingRate = sr;
        channels = ch;

        if (fmt != AV_SAMPLE_FMT_FLT)
            log.error("Some code assumes float input");

        log.assert_quit(ch == 1 || ch == 2, "Unsupported audio channel count: {}", ch);

        int64_t layout = channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;

        swrCtx = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLT, 48000, layout, fmt, samplingRate,
                                    0, nullptr);
        swr_init(swrCtx);

        int err;
        enc = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &err);
        log.assert_quit(enc != nullptr && err == OPUS_OK, "Failed to create opus encoder");
    });
    cap->setOnAudioData([this](const uint8_t* data, size_t len) {
        // FIXME: Assumes input to be float
        std::lock_guard lock(bufferLock);
        int maxOutput = swr_get_out_samples(swrCtx, len / channels / sizeof(float));
        ByteBuffer& now = buffer.emplace_back(maxOutput * channels * sizeof(float));

        uint8_t* outPtr = now.data();
        int stat = swr_convert(swrCtx, &outPtr, maxOutput, &data, len / channels / sizeof(float));
        log.assert_quit(0 <= stat, "Failed to call swr_convert");
        if (stat != maxOutput) {
            // log->warn("Resizing output buffer");
            now.resize(stat * channels * sizeof(float));
        }
        bufferLockCV.notify_one();
    });
}

AudioEncoder::~AudioEncoder() {
    if (workerThread.joinable())
        workerThread.join();
}

void AudioEncoder::start() {
    int stat;

    samplingRate = -1;
    channels = -1;

    if (workerThread.joinable())
        workerThread.join();

    flagRun.store(true, std::memory_order_release);
    workerThread = std::thread(&AudioEncoder::runWorker_, this);

    cap->start();
}

void AudioEncoder::stop() {
    cap->stop();

    flagRun.store(false, std::memory_order_release);
    bufferLockCV.notify_all();
    // workerThread.join();
    // Can't join here because workerThread may be the one calling stop; I don't like it

    buffer.clear();
    swr_free(&swrCtx);
    opus_encoder_destroy(enc);
    enc = nullptr;
}

void AudioEncoder::runWorker_() {
    const int frameSize = 960;
    // FIXME: Assuming stereo
    ByteBuffer buf(frameSize * sizeof(float) * 2);
    int writeIdx = 0;

    // constrain max bitrate
    ByteBuffer output(256'000 / 8 * 20 / 1000);

    memset(buf.data(), 0, buf.size());

    while (flagRun.load(std::memory_order_acquire)) {
        ByteBuffer curr;

        /* lock */ {
            std::unique_lock lock(bufferLock);
            while (buffer.empty() && flagRun.load(std::memory_order_acquire))
                bufferLockCV.wait(lock);

            if (!buffer.empty()) {
                curr = std::move(buffer.front());
                buffer.pop_front();
            } else {
                // flag run is now false
                break;
            }
        }

        int readIdx = 0;
        while (readIdx < curr.size()) {
            int readAmount = std::min(curr.size() - readIdx, buf.size() - writeIdx);
            memcpy(buf.data() + writeIdx, curr.data() + readIdx, readAmount);
            readIdx += readAmount;
            writeIdx += readAmount;

            if (writeIdx == buf.size()) {
                writeIdx = 0;
                int stat = opus_encode_float(enc, reinterpret_cast<float*>(buf.data()), frameSize, output.data(),
                                             output.size());
                log.assert_quit(0 <= stat, "Failed to call opus_encode_float");
                onAudioData(output.data(), stat);
            }
        }
    }
}
