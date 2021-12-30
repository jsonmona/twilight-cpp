#ifndef TWILIGHT_COMMON_FFMPEG-HEADERS_H
#define TWILIGHT_COMMON_FFMPEG-HEADERS_H

#include <cstdint>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4819)
#endif

extern "C" {
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <libavutil/imgutils.h>
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif