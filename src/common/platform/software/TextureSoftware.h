#ifndef SERVER_PLATFORM_SOFTWARE_TEXTURE_SOFTWARE_H_
#define SERVER_PLATFORM_SOFTWARE_TEXTURE_SOFTWARE_H_


#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
}

struct TextureSoftware {
	int width, height;
	AVPixelFormat format;
	int linesize[4];
	uint8_t* data[4];
	uint8_t* allocated;

	TextureSoftware();
	TextureSoftware(const TextureSoftware& copy) = delete;
	TextureSoftware(TextureSoftware&& move) noexcept;
	~TextureSoftware();

	TextureSoftware& operator=(const TextureSoftware& copy) = delete;
	TextureSoftware& operator=(TextureSoftware&& move) noexcept;

	static TextureSoftware allocate(int w, int h, AVPixelFormat fmt);
	static TextureSoftware reference(uint8_t** data, const int* linesize, int w, int h, AVPixelFormat fmt);

	void release();

	TextureSoftware clone() const;
};


#endif