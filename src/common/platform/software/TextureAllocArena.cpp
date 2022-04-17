#include "TextureAllocArena.h"

#include <algorithm>

#include "common/platform/software/TextureSoftware.h"

TWILIGHT_DEFINE_LOGGER(TextureAllocArena);

void ensureFormat(std::shared_ptr<TextureAllocArena>* arena, int w, int h, AVPixelFormat fmt) {
    if (*arena == nullptr || !(*arena)->checkConfig(w, h, fmt))
        *arena = TextureAllocArena::getArena(w, h, fmt);
}

void swap(TextureAllocArena& a, TextureAllocArena& b) noexcept {
    using std::swap;

    std::scoped_lock lock(a.lock, b.lock);
    swap(a.self, b.self);
    swap(a.width, b.width);
    swap(a.height, b.height);
    swap(a.format, b.format);
    swap(a.textureSize, b.textureSize);
    swap(a.blocks, b.blocks);
}

TextureAllocArena::TextureAllocArena()
    : width(-1), height(-1), format(AV_PIX_FMT_NONE), lastBlockId(0), textureSize(0) {}

TextureAllocArena::TextureAllocArena(TextureAllocArena&& move) noexcept
    : width(-1), height(-1), format(AV_PIX_FMT_NONE), lastBlockId(0), textureSize(0) {
    swap(*this, move);
}

TextureAllocArena& TextureAllocArena::operator=(TextureAllocArena&& move) noexcept {
    swap(*this, move);
    return *this;
}

std::shared_ptr<TextureAllocArena> TextureAllocArena::getArena(int w, int h, AVPixelFormat fmt) {
    // TODO: Globally share and return existing one if possible

    auto ret = std::shared_ptr<TextureAllocArena>(new TextureAllocArena());
    int err;

    ret->self = ret;
    ret->width = w;
    ret->height = h;
    ret->format = fmt;
    ret->lastBlockId = 0;

    int linesize[4];
    err = av_image_fill_linesizes(linesize, fmt, w);
    log.assert_quit(0 <= err, "Failed to fill linesize");

    ptrdiff_t linesizes[4];
    for (int i = 0; i < 4; i++)
        linesizes[i] = linesize[i];

    size_t planeSize[4];
    err = av_image_fill_plane_sizes(planeSize, fmt, h, linesizes);
    log.assert_quit(0 <= err, "Failed to get plane sizes");

    size_t totalSize = 0;
    for (int i = 0; i < 4; i++)
        totalSize += planeSize[i];

    ret->textureSize = totalSize;
    return ret;
}

TextureAllocArena::~TextureAllocArena() {
    // Since TextureSoftware has a shared_ptr pointing this,
    // this object should not destruct until all textures are released.
    std::lock_guard lk(lock);
    blocks.clear();
}

bool TextureAllocArena::checkConfig(int w, int h, AVPixelFormat fmt) const {
    return w == width && h == height && format == fmt;
}

TextureSoftware TextureAllocArena::alloc() {
    TextureSoftware ret;
    int err;

    ret.arena = self.lock();
    log.assert_quit(!!ret.arena, "self pointer destructed!");

    Block* blk = nullptr;

    ret.width = width;
    ret.height = height;
    ret.format = format;
    ret.blockId = std::numeric_limits<size_t>::max();
    ret.blockSlot = -1;

    std::lock_guard lk(lock);

    for (auto itr = blocks.begin(); itr != blocks.end(); ++itr) {
        int sel = itr->allocSlot();
        if (sel != -1) {
            blk = &*itr;
            ret.blockId = blk->id();
            ret.blockSlot = sel;
            break;
        }
    }

    if (blk == nullptr) {
        // All blocks are full, allocate another.
        size_t blockId = ++lastBlockId;
        blocks.push_back(Block(textureSize, blockId));
        blk = &blocks.back();
        ret.blockId = blockId;
        ret.blockSlot = blk->allocSlot();
    }

    if (blk == nullptr)
        abort();  // A block should be assigned at this point

    err = av_image_fill_linesizes(ret.linesize, ret.format, ret.width);
    log.assert_quit(0 <= err, "Failed to fill linesize");

    ptrdiff_t linesizes[4];
    for (int i = 0; i < 4; i++)
        linesizes[i] = ret.linesize[i];

    size_t planeSize[4];
    err = av_image_fill_plane_sizes(planeSize, ret.format, ret.height, linesizes);
    log.assert_quit(0 <= err, "Failed to get plane sizes");

    size_t totalSize = 0;
    for (int i = 0; i < 4; i++)
        totalSize += planeSize[i];
    log.assert_quit(textureSize == totalSize, "Wrong textureSize!");

    uint8_t* p = (*blk)[ret.blockSlot];
    for (int i = 0; i < 4; i++) {
        if (ret.linesize[i] == 0)
            break;
        ret.data[i] = p;
        p += planeSize[i];
    }

    return ret;
}

TextureAllocArena::Block* TextureAllocArena::findBlock(size_t blockId) {
    std::lock_guard lk(lock);
    return findBlock_locked(blockId);
}

void TextureAllocArena::gc() {
    std::lock_guard lk(lock);

    while (true) {
        // Loop until there are no entries to delete
        bool keepIterating = true;
        for (auto itr = blocks.begin(); keepIterating && itr != blocks.end(); ++itr) {
            if (itr->checkAllEmpty()) {
                blocks.erase(itr);
                keepIterating = false;
            }
        }
        if (keepIterating)
            break;
    }
}

TextureAllocArena::Block* TextureAllocArena::findBlock_locked(size_t blockId) {
    for (auto itr = blocks.begin(); itr != blocks.end(); ++itr)
        if (itr->id() == blockId)
            return &*itr;
    return nullptr;
}

TextureAllocArena::Block::Block(size_t size, size_t blockId) : size(size), blockId(blockId) {
    for (int i = 0; i < BLOCK_SIZE; i++)
        available[i].store(true, std::memory_order_relaxed);
    data = reinterpret_cast<uint8_t*>(av_malloc(size * BLOCK_SIZE));
}

TextureAllocArena::Block::Block(Block&& move) noexcept {
    data = nullptr;
    size = 0;
    blockId = 0;
    for (int i = 0; i < BLOCK_SIZE; i++)
        available[i].store(true, std::memory_order_relaxed);

    *this = std::move(move);
}

TextureAllocArena::Block& TextureAllocArena::Block::operator=(Block&& move) noexcept {
    // This must swap or move ctor will break

    using std::swap;

    swap(data, move.data);
    swap(size, move.size);
    swap(blockId, move.blockId);
    for (int i = 0; i < BLOCK_SIZE; i++) {
        bool temp = available[i].load(std::memory_order_relaxed);
        temp = move.available[i].exchange(temp, std::memory_order_relaxed);
        available[i].store(temp, std::memory_order_relaxed);
    }

    return *this;
}

TextureAllocArena::Block::~Block() {
    if (data == nullptr)
        return;

    for (int i = 0; i < BLOCK_SIZE; i++) {
        if (!available[i].load(std::memory_order_relaxed))
            abort();  // Destructed while something's allocated
    }

    av_freep(&data);
}

int TextureAllocArena::Block::allocSlot() {
    for (int i = 0; i < BLOCK_SIZE; i++) {
        if (available[i].exchange(false, std::memory_order_relaxed))
            return i;
    }
    return -1;
}

void TextureAllocArena::Block::freeSlot(int slot) {
    if (slot < 0 || BLOCK_SIZE <= slot)
        abort();  // Index out of range
    if (available[slot].exchange(true, std::memory_order_relaxed))
        abort();  // Double free
}

bool TextureAllocArena::Block::checkAllEmpty() {
    for (int i = 0; i < BLOCK_SIZE; i++)
        if (!available[i].load(std::memory_order_relaxed))
            return false;
    return true;
}
