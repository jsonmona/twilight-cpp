#ifndef TWILIGHT_COMMON_FFMPEGHEADERS_H
#define TWILIGHT_COMMON_FFMPEGHEADERS_H

#include <cstdint>

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

#endif