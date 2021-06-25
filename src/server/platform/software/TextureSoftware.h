#ifndef SERVER_PLATFORM_SOFTWARE_TEXTURE_SOFTWARE_H_
#define SERVER_PLATFORM_SOFTWARE_TEXTURE_SOFTWARE_H_


#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
}

struct TextureSoftware {
	int width, height;
	AVPixelFormat format;
	int linesize[4];
	uint8_t* data[4];

	TextureSoftware();
	TextureSoftware(int w, int h, AVPixelFormat fmt);
	TextureSoftware(const TextureSoftware& copy);
	TextureSoftware(TextureSoftware&& move) noexcept;
	~TextureSoftware();

	TextureSoftware& operator=(const TextureSoftware& copy);
	TextureSoftware& operator=(TextureSoftware&& move) noexcept;
};


#endif