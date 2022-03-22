#include "TextureSoftware.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>

#include "common/log.h"
#include "common/util.h"

void swap(TextureSoftware &a, TextureSoftware &b) {
    using std::swap;

    swap(a.width, b.width);
    swap(a.height, b.height);
    swap(a.format, b.format);
    for (int i = 0; i < 4; i++)
        swap(a.linesize[i], b.linesize[i]);
    for (int i = 0; i < 4; i++)
        swap(a.data[i], b.data[i]);
    swap(a.arena, b.arena);
    swap(a.blockId, b.blockId);
    swap(a.blockSlot, b.blockSlot);
}

TextureSoftware::TextureSoftware() {
    width = -1;
    height = -1;
    format = AV_PIX_FMT_NONE;
    std::fill(linesize, linesize + 4, 0);
    std::fill(data, data + 4, nullptr);
    arena.reset();
    blockId = 0;
    blockSlot = -1;
}

TextureSoftware::TextureSoftware(TextureSoftware &&move) noexcept {
    width = -1;
    height = -1;
    format = AV_PIX_FMT_NONE;
    std::fill(linesize, linesize + 4, 0);
    std::fill(data, data + 4, nullptr);
    arena.reset();
    blockId = 0;
    blockSlot = -1;

    swap(*this, move);
}

TextureSoftware::~TextureSoftware() {
    if (arena) {
        arena->findBlock(blockId)->freeSlot(blockSlot);
        blockId = 0;
        blockSlot = -1;
        arena.reset();
    }
}

TextureSoftware &TextureSoftware::operator=(TextureSoftware &&move) noexcept {
    swap(*this, move);
    return *this;
}

TextureSoftware TextureSoftware::reference(uint8_t **data, const int *linesize, int w, int h, AVPixelFormat fmt) {
    TextureSoftware self;

    self.width = w;
    self.height = h;
    self.format = fmt;

    const int planeCount = av_pix_fmt_count_planes(fmt);
    if (planeCount > 4)
        abort();  // Buffer overflow
    std::copy(data, data + planeCount, self.data);
    std::copy(linesize, linesize + planeCount, self.linesize);

    return self;
}

TextureSoftware TextureSoftware::clone(TextureAllocArena *targetArena) const {
    if (targetArena == nullptr)
        abort();  // nullptr not supported at this moment

    if (arena.get() != targetArena &&
        (targetArena->width != width || targetArena->height != height || targetArena->format != format))
        error_quit(targetArena->log, "Format mismatch while cloning!");

    TextureSoftware ret = targetArena->alloc();

    av_image_copy(ret.data, ret.linesize, const_cast<const uint8_t **>(data), linesize, format, width, height);

    return ret;
}

bool TextureSoftware::isEmpty() const {
    return width < 0 || height < 0;
}
