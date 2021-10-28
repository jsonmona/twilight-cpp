#include "util.h"

#include <cstdio>


std::optional<ByteBuffer> loadEntireFile(const char* path) {
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


bool writeByteBuffer(const char* filename, const ByteBuffer& data) {
	FILE* f = fopen(filename, "wb");
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