#include "AudioCaptureWASAPI.h"

static AVSampleFormat wave2avsample(const WAVEFORMATEXTENSIBLE& fmt) {
    if (fmt.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (fmt.SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
            if (fmt.Format.wBitsPerSample == 8)
                return AV_SAMPLE_FMT_U8;
            else if (fmt.Format.wBitsPerSample == 16)
                return AV_SAMPLE_FMT_S16;
            else if (fmt.Format.wBitsPerSample == 32)
                return AV_SAMPLE_FMT_S32;
            else
                return AV_SAMPLE_FMT_NONE;
        } else if (fmt.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            if (fmt.Format.wBitsPerSample == 32)
                return AV_SAMPLE_FMT_FLT;
            else if (fmt.Format.wBitsPerSample == 64)
                return AV_SAMPLE_FMT_DBL;
            else
                return AV_SAMPLE_FMT_NONE;
        } else {
            return AV_SAMPLE_FMT_NONE;
        }
    } else {
        // TODO: parse wFormatTag
        return AV_SAMPLE_FMT_NONE;
    }
}

AudioCaptureWASAPI::AudioCaptureWASAPI() : log(createNamedLogger("AudioCaptureWASAPI")) {}

AudioCaptureWASAPI::~AudioCaptureWASAPI() {}

void AudioCaptureWASAPI::start() {
    timeBeginPeriod(1);

    flagRun.store(true, std::memory_order_release);
    playbackThread = std::thread(&AudioCaptureWASAPI::runPlayback_, this);
    recordThread = std::thread(&AudioCaptureWASAPI::runRecord_, this);
}

void AudioCaptureWASAPI::stop() {
    flagRun.store(false, std::memory_order_release);
    recordThread.join();
    playbackThread.join();

    timeEndPeriod(1);
}

void AudioCaptureWASAPI::runRecord_() {
    HRESULT hr;

    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    check_quit(FAILED(hr), log, "Failed to initialize COM");

    ComWrapper<IMMDeviceEnumerator> devEnumerator;

    const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
    const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator, devEnumerator.data());
    check_quit(FAILED(hr), log, "Failed to create audio device enumerator");

    ComWrapper<IMMDevice> endpoint;
    hr = devEnumerator->GetDefaultAudioEndpoint(eRender, ERole::eConsole, endpoint.data());
    check_quit(FAILED(hr), log, "Failed to get default audio endpoint");
    // TODO: Properly handle cases where no audio device is installed

    ComWrapper<IAudioClient> audioClient;
    endpoint->Activate(audioClient.guid(), CLSCTX_ALL, nullptr, audioClient.data());

    WAVEFORMATEXTENSIBLE waveformat;

    WAVEFORMATEX* waveformatUsed;
    hr = audioClient->GetMixFormat(&waveformatUsed);
    check_quit(FAILED(hr) || waveformatUsed == nullptr, log, "Failed to retrieve mix format");
    check_quit(waveformatUsed->cbSize > 22, log, "Mix format larger than WAVEFORMATEXTENSIBLE");
    memcpy(&waveformat, waveformatUsed, sizeof(WAVEFORMATEX) + waveformatUsed->cbSize);
    CoTaskMemFree(waveformatUsed);

    const REFERENCE_TIME bufferTime = 10'000;  // 10ms

    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, bufferTime, 0,
                                 &waveformat.Format, nullptr);
    check_quit(FAILED(hr), log, "Failed to initialize audio client as loopback mode");

    AVSampleFormat av_fmt = wave2avsample(waveformat);
    int sizeofSample = waveformat.Format.wBitsPerSample == 8    ? 1
                       : waveformat.Format.wBitsPerSample == 16 ? 2
                       : waveformat.Format.wBitsPerSample == 32 ? 4
                       : waveformat.Format.wBitsPerSample == 64 ? 8
                                                                : 0;
    check_quit(av_fmt == AV_SAMPLE_FMT_NONE || sizeofSample == 0, log,
               "Failed to determine appropriate representation of waveformat");
    onConfigured(av_fmt, waveformat.Format.nSamplesPerSec, waveformat.Format.nChannels);

    bool receivedFirstFrame = false;
    ByteBuffer silenceBuffer;

    ComWrapper<IAudioCaptureClient> audioCaptureClient;
    hr = audioClient->GetService(audioCaptureClient.guid(), audioCaptureClient.data());
    check_quit(FAILED(hr), log, "Failed to get audio capture client");

    hr = audioClient->Start();
    check_quit(FAILED(hr), log, "Failed to start audio capture client");

    while (flagRun.load(std::memory_order_acquire)) {
        BYTE* data;
        UINT32 numFramesToRead;
        DWORD flags;
        UINT64 qpcPos;
        hr = audioCaptureClient->GetBuffer(&data, &numFramesToRead, &flags, nullptr, &qpcPos);
        if (hr >= 0) {
            if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) {
                if (receivedFirstFrame)
                    log->error("Received another timestamp error!");
                Sleep(1);
                continue;
            } else {
                receivedFirstFrame = true;
            }

            if (numFramesToRead > 0) {
                int byteLen = numFramesToRead * sizeofSample * waveformat.Format.nChannels;

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    if (silenceBuffer.size() < byteLen) {
                        // TODO: Properly handle unsigned sample formats
                        size_t prevSize = silenceBuffer.size();
                        silenceBuffer.resize(byteLen);
                        memset(silenceBuffer.data() + prevSize, 0, byteLen - prevSize);
                    }
                    onAudioData(silenceBuffer.data(), byteLen);
                } else
                    onAudioData(data, byteLen);
            }

            audioCaptureClient->ReleaseBuffer(numFramesToRead);
        } else {
            error_quit(log, "Unknown error from GetBuffer: {:#x}", hr);
        }

        // TODO: increase sleep amount
        Sleep(1);
    }

    hr = audioClient->Stop();
    check_quit(FAILED(hr), log, "Failed to stop audio client");

    CoUninitialize();
}

void AudioCaptureWASAPI::runPlayback_() {
    HRESULT hr;

    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    check_quit(FAILED(hr), log, "Failed to initialize COM");

    ComWrapper<IMMDeviceEnumerator> devEnumerator;

    const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
    const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator, devEnumerator.data());
    check_quit(FAILED(hr), log, "Failed to create audio device enumerator");

    ComWrapper<IMMDevice> endpoint;
    hr = devEnumerator->GetDefaultAudioEndpoint(eRender, ERole::eConsole, endpoint.data());
    check_quit(FAILED(hr), log, "Failed to get default audio endpoint");
    // TODO: Properly handle cases where no audio device is installed

    ComWrapper<IAudioClient> audioClient;
    endpoint->Activate(audioClient.guid(), CLSCTX_ALL, nullptr, audioClient.data());

    WAVEFORMATEXTENSIBLE waveformat;

    WAVEFORMATEX* waveformatUsed;
    hr = audioClient->GetMixFormat(&waveformatUsed);
    check_quit(FAILED(hr) || waveformatUsed == nullptr, log, "Failed to retrieve mix format");
    check_quit(waveformatUsed->cbSize > 22, log, "Mix format larger than WAVEFORMATEXTENSIBLE");
    memcpy(&waveformat, waveformatUsed, sizeof(WAVEFORMATEX) + waveformatUsed->cbSize);
    CoTaskMemFree(waveformatUsed);

    const REFERENCE_TIME bufferTime = 50'000;  // 50ms

    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, bufferTime, 0, &waveformat.Format, nullptr);
    check_quit(FAILED(hr), log, "Failed to initialize audio playback client");

    AVSampleFormat av_fmt = wave2avsample(waveformat);
    int sizeofSample = waveformat.Format.wBitsPerSample == 8    ? 1
                       : waveformat.Format.wBitsPerSample == 16 ? 2
                       : waveformat.Format.wBitsPerSample == 32 ? 4
                       : waveformat.Format.wBitsPerSample == 64 ? 8
                                                                : 0;
    check_quit(av_fmt == AV_SAMPLE_FMT_NONE || sizeofSample == 0, log,
               "Failed to determine appropriate representation of waveformat");

    ComWrapper<IAudioRenderClient> audioRenderClient;
    hr = audioClient->GetService(audioRenderClient.guid(), audioRenderClient.data());
    check_quit(FAILED(hr), log, "Failed to get audio render client");

    UINT32 bufferFrameCnt;
    audioClient->GetBufferSize(&bufferFrameCnt);

    BYTE* data;
    hr = audioRenderClient->GetBuffer(bufferFrameCnt, &data);
    check_quit(FAILED(hr), log, "Failed to retrieve buffer for initial fill");
    memset(data, 0, bufferFrameCnt * sizeofSample * waveformat.Format.nChannels);
    hr = audioRenderClient->ReleaseBuffer(bufferFrameCnt, 0);
    check_quit(FAILED(hr), log, "Failed to release buffer for initial fill");

    hr = audioClient->Start();
    check_quit(FAILED(hr), log, "Failed to start audio render client");

    while (flagRun.load(std::memory_order_acquire)) {
        UINT32 currPad;
        hr = audioClient->GetCurrentPadding(&currPad);
        check_quit(FAILED(hr), log, "Failed to get current padding");

        if (currPad < bufferFrameCnt) {
            int framesToWrite = bufferFrameCnt - currPad;
            hr = audioRenderClient->GetBuffer(framesToWrite, &data);
            if (hr >= 0) {
                audioRenderClient->ReleaseBuffer(framesToWrite, AUDCLNT_BUFFERFLAGS_SILENT);
            } else {
                error_quit(log, "Unknown error from GetBuffer: {:#x}", hr);
            }
        }

        // TODO: increase sleep amount
        Sleep(1);
    }

    hr = audioClient->Stop();
    check_quit(FAILED(hr), log, "Failed to stop audio client");

    CoUninitialize();
}
