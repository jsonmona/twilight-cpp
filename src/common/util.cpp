#include "util.h"

#include <mbedtls/sha256.h>

#include <cstdio>

std::optional<ByteBuffer> loadEntireFile(const char *path) {
    std::optional<ByteBuffer> ret;

    long filesize;
    FILE *f = fopen(path, "rb");

    if (f == nullptr)
        goto failed;

    if (fseek(f, 0, SEEK_END) != 0)
        goto failed;

    filesize = ftell(f);
    if (filesize < 0 || 1024 * 1024 * 1024 < filesize)
        goto failed;

    if (fseek(f, 0, SEEK_SET) != 0)
        goto failed;

    ret.emplace(filesize);

    long pos = 0;
    while (pos < filesize) {
        size_t stat = fread(ret->data() + pos, 1, filesize - pos, f);
        if (stat <= 0) {
            ret.reset();
            break;
        }
        pos += stat;
    }

failed:
    if (f != nullptr) {
        fclose(f);
        f = nullptr;
    }

    return ret;
}

bool writeByteBuffer(const char *filename, const ByteBuffer &data) {
    FILE *f = fopen(filename, "wb");
    size_t offset = 0;
    while (offset < data.size()) {
        size_t write = fwrite(data.data() + offset, 1, data.size() - offset, f);
        if (write == 0)
            break;
        offset += write;
    }
    fclose(f);

    return offset == data.size();
}

bool secureMemcmp(const void *a, const void *b, size_t bytes) {
    const volatile unsigned char *pa = reinterpret_cast<const volatile unsigned char *>(a);
    const volatile unsigned char *pb = reinterpret_cast<const volatile unsigned char *>(b);

    unsigned char x = 0;
    for (size_t i = 0; i < bytes; i++)
        x |= pa[i] ^ pb[i];

    return x == 0;
}

ByteBuffer hashBytesSHA256(const ByteBuffer &raw) {
    ByteBuffer ret(32);
    int status = mbedtls_sha256_ret(raw.data(), raw.size(), ret.data(), 0);
    if (status != 0)
        abort();  // Unlikely to happen
    return ret;
}
