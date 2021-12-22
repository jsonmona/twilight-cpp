#ifndef COMMON_FFMPEG_HEADERS_H_
#define COMMON_FFMPEG_HEADERS_H_


#include <cstdint>

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4819)
#endif

extern "C" {
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <libavutil/imgutils.h>
}

#ifdef _MSC_VER
#pragma warning (pop)
#endif


#endif