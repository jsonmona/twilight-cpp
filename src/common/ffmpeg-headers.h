#ifndef TWILIGHT_COMMON_FFMPEGHEADERS_H
#define TWILIGHT_COMMON_FFMPEGHEADERS_H

#include <cstdint>
#include <utility>
#include <algorithm>

// clang-format disable

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4819)
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <libavutil/imgutils.h>
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

// clang-format enable

class AVFramePtr {
public:
    AVFramePtr() : ptr(av_frame_alloc()) {}
    AVFramePtr(const AVFramePtr& copy) = delete;
    AVFramePtr(AVFramePtr&& move) noexcept {
        using std::swap;
        ptr = nullptr;
        swap(ptr, move.ptr);
    }
    ~AVFramePtr() { av_frame_free(&ptr); }

    AVFramePtr& operator=(const AVFramePtr& copy) = delete;
    AVFramePtr& operator=(AVFramePtr&& move) {
        using std::swap;
        swap(ptr, move.ptr);
        return *this;
    }

    AVFrame* get() { return ptr; }
    AVFrame** data() { return &ptr; }
    AVFrame* operator->() { return get(); }

    void alloc() {
        if (ptr == nullptr)
            ptr = av_frame_alloc();
    }

    void free() { av_frame_free(&ptr); }

private:
    AVFrame* ptr;
};

class AVPacketPtr {
public:
    AVPacketPtr() : ptr(av_packet_alloc()) {}
    AVPacketPtr(const AVPacketPtr& copy) = delete;
    AVPacketPtr(AVPacketPtr&& move) noexcept {
        using std::swap;
        ptr = nullptr;
        swap(ptr, move.ptr);
    }
    ~AVPacketPtr() { av_packet_free(&ptr); }

    AVPacketPtr& operator=(const AVPacketPtr& copy) = delete;
    AVPacketPtr& operator=(AVPacketPtr&& move) {
        using std::swap;
        swap(ptr, move.ptr);
        return *this;
    }

    AVPacket* get() { return ptr; }
    AVPacket** data() { return &ptr; }
    AVPacket* operator->() { return get(); }

    void alloc() {
        if (ptr == nullptr)
            ptr = av_packet_alloc();
    }

    void free() { av_packet_free(&ptr); }

private:
    AVPacket* ptr;
};

#endif