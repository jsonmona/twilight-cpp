#ifndef TWILIGHT_COMMON_PLATFORM_SOFTWARE_TEXTURESOFTWARE_H
#define TWILIGHT_COMMON_PLATFORM_SOFTWARE_TEXTURESOFTWARE_H

#include "common/ffmpeg-headers.h"

#include "common/platform/software/TextureAllocArena.h"

#include <memory>

class TextureSoftware {
    friend class TextureAllocArena;

public:
    friend void swap(TextureSoftware &a, TextureSoftware &b);

    TextureSoftware();
    TextureSoftware(const TextureSoftware &copy) = delete;
    TextureSoftware(TextureSoftware &&move) noexcept;
    ~TextureSoftware();

    TextureSoftware &operator=(const TextureSoftware &copy) = delete;
    TextureSoftware &operator=(TextureSoftware &&move) noexcept;

    static TextureSoftware reference(uint8_t **data, const int *linesize, int w, int h, AVPixelFormat fmt);

    TextureSoftware clone(TextureAllocArena* targetArena) const;
    TextureSoftware clone(TextureAllocArena &targetArena) const { return clone(&targetArena); }
    TextureSoftware clone(std::shared_ptr<TextureAllocArena> &targetArena) const { return clone(targetArena.get()); }

    bool isEmpty() const;

public:
    int width, height;
    AVPixelFormat format;
    int linesize[4];
    uint8_t *data[4];

private:
    std::shared_ptr<TextureAllocArena> arena;
    size_t blockId;
    int blockSlot;
};

#endif