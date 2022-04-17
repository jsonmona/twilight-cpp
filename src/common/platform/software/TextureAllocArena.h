#ifndef TWILIGHT_COMMON_PLATFORM_SOFTWARE_TEXTUREALLOCARENA_H
#define TWILIGHT_COMMON_PLATFORM_SOFTWARE_TEXTUREALLOCARENA_H

#include "common/ffmpeg-headers.h"
#include "common/log.h"

#include <memory>
#include <mutex>
#include <vector>

class TextureSoftware;
class TextureAllocArena;

// Recreate TextureAllocArena if format differs
void ensureFormat(std::shared_ptr<TextureAllocArena>* arena, int w, int h, AVPixelFormat fmt);

class TextureAllocArena {
    friend class TextureSoftware;
    class Block;

public:
    friend void swap(TextureAllocArena& a, TextureAllocArena& b) noexcept;

    TextureAllocArena(const TextureAllocArena& copy) = delete;
    TextureAllocArena(TextureAllocArena&& move) noexcept;
    TextureAllocArena& operator=(const TextureAllocArena& copy) = delete;
    TextureAllocArena& operator=(TextureAllocArena&& move) noexcept;

    static std::shared_ptr<TextureAllocArena> getArena(int w, int h, AVPixelFormat fmt);

    ~TextureAllocArena();

    bool checkConfig(int w, int h, AVPixelFormat fmt) const;

    TextureSoftware alloc();
    Block* findBlock(size_t blockId);

    void gc();

private:
    TextureAllocArena();
    Block* findBlock_locked(size_t blockId);

    class Block {
    public:
        static constexpr int BLOCK_SIZE = 8;

        Block(size_t size, size_t blockId);
        Block(const Block& copy) = delete;
        Block(Block&& move) noexcept;

        Block& operator=(const Block& copy) = delete;
        Block& operator=(Block&& move) noexcept;

        ~Block();

        int allocSlot();
        void freeSlot(int slot);
        bool checkAllEmpty();
        size_t id() const { return blockId; }
        uint8_t* operator[](int idx) { return data + size * idx; }

    private:
        uint8_t* data;
        size_t size;
        size_t blockId;
        std::atomic<bool> available[BLOCK_SIZE];
    };

    static NamedLogger log;
    std::weak_ptr<TextureAllocArena> self;

    int width, height;
    AVPixelFormat format;
    size_t textureSize;
    size_t lastBlockId;

    std::mutex lock;
    std::vector<Block> blocks;
};

#endif
