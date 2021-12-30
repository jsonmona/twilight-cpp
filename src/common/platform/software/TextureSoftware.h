#ifndef TWILIGHT_COMMON_PLATFORM_SOFTWARE_TEXTURESOFTWARE_H
#define TWILIGHT_COMMON_PLATFORM_SOFTWARE_TEXTURESOFTWARE_H

#include "common/ffmpeg-headers.h"

#include <memory>

struct TextureSoftware {
    int width, height;
    AVPixelFormat format;
    int linesize[4];
    uint8_t *data[4];
    uint8_t *allocated;

    TextureSoftware();
    TextureSoftware(const TextureSoftware &copy) = delete;
    TextureSoftware(TextureSoftware &&move) noexcept;
    ~TextureSoftware();

    TextureSoftware &operator=(const TextureSoftware &copy) = delete;
    TextureSoftware &operator=(TextureSoftware &&move) noexcept;

    static TextureSoftware allocate(int w, int h, AVPixelFormat fmt);
    static TextureSoftware reference(uint8_t **data, const int *linesize, int w, int h, AVPixelFormat fmt);

    void release();

    TextureSoftware clone() const;
};

#endif