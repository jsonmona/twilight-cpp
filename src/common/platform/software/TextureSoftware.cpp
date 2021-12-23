#include "TextureSoftware.h"

#include "common/log.h"

TextureSoftware::TextureSoftware() {
    width = -1;
    height = -1;
    format = AV_PIX_FMT_NONE;
    allocated = nullptr;
    std::fill(linesize, linesize + 4, 0);
    std::fill(data, data + 4, nullptr);
}

TextureSoftware::TextureSoftware(TextureSoftware &&move) noexcept {
    width = move.width;
    height = move.height;
    format = move.format;
    allocated = move.allocated;
    std::copy(move.linesize, move.linesize + 4, linesize);
    std::copy(move.data, move.data + 4, data);

    std::fill(move.data, move.data + 4, nullptr);
    move.allocated = nullptr;
}

TextureSoftware::~TextureSoftware() {
    av_free(allocated);
}

TextureSoftware &TextureSoftware::operator=(TextureSoftware &&move) noexcept {
    av_free(allocated);

    width = move.width;
    height = move.height;
    format = move.format;
    allocated = move.allocated;
    std::copy(move.linesize, move.linesize + 4, linesize);
    std::copy(move.data, move.data + 4, data);

    std::fill(move.data, move.data + 4, nullptr);
    move.allocated = nullptr;
    return *this;
}

TextureSoftware TextureSoftware::allocate(int w, int h, AVPixelFormat fmt) {
    TextureSoftware self;

    self.width = w;
    self.height = h;
    self.format = fmt;

    int stat = av_image_alloc(self.data, self.linesize, w, h, fmt, 32);
    if (stat < 0)
        error_quit(spdlog::default_logger(), "Failed to create TextureSoftware");
    self.allocated = self.data[0];

    return self;
}

TextureSoftware TextureSoftware::reference(uint8_t **data, const int *linesize, int w, int h, AVPixelFormat fmt) {
    TextureSoftware self;

    self.width = w;
    self.height = h;
    self.format = fmt;

    const int planeCount = av_pix_fmt_count_planes(fmt);
    std::copy(data, data + planeCount, self.data);
    std::copy(linesize, linesize + planeCount, self.linesize);

    return self;
}

void TextureSoftware::release() {
    allocated = nullptr;
}

TextureSoftware TextureSoftware::clone() const {
    TextureSoftware tex = allocate(width, height, format);

    av_image_copy(tex.data, tex.linesize, const_cast<const uint8_t **>(data), linesize, format, width, height);

    return tex;
}
