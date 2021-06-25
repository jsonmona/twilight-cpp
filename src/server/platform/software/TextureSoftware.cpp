#include "TextureSoftware.h"

#include "common/log.h"

extern "C" {
#include <libavutil/imgutils.h>
}


TextureSoftware::TextureSoftware() {
	width = -1;
	height = -1;
	format = AV_PIX_FMT_NONE;
	std::fill(linesize, linesize + 4, 0);
	std::fill(data, data + 4, nullptr);
}

TextureSoftware::TextureSoftware(int w, int h, AVPixelFormat fmt) {
	width = w;
	height = h;
	format = fmt;
	std::fill(linesize, linesize + 4, 0);
	std::fill(data, data + 4, nullptr);

	int stat = av_image_alloc(data, linesize, w, h, fmt, 32);
	if (stat < 0)
		error_quit(spdlog::default_logger(), "Failed to create TextureSoftware");
}

TextureSoftware::TextureSoftware(const TextureSoftware& copy) {
	width = copy.width;
	height = copy.height;
	format = copy.format;
	std::fill(linesize, linesize + 4, 0);
	std::fill(data, data + 4, nullptr);

	int stat = av_image_alloc(data, linesize, width, height, format, 32);
	if (stat < 0)
		error_quit(spdlog::default_logger(), "Failed to copy TextureSoftware");

	// TODO: Validate const_cast of next line
	av_image_copy(data, linesize, const_cast<const uint8_t**>(copy.data), copy.linesize, format, width, height);
}

TextureSoftware::TextureSoftware(TextureSoftware&& move) noexcept {
	width = move.width;
	height = move.height;
	format = move.format;
	std::copy(move.linesize, move.linesize + 4, linesize);
	std::copy(move.data, move.data + 4, data);

	std::fill(move.data, move.data + 4, nullptr);
}

TextureSoftware::~TextureSoftware() {
	av_free(data[0]);
}

TextureSoftware& TextureSoftware::operator=(const TextureSoftware& copy) {
	av_free(data[0]);

	width = copy.width;
	height = copy.height;
	format = copy.format;
	std::fill(linesize, linesize + 4, 0);
	std::fill(data, data + 4, nullptr);

	int stat = av_image_alloc(data, linesize, width, height, format, 32);
	if (stat < 0)
		error_quit(spdlog::default_logger(), "Failed to copy TextureSoftware");

	// TODO: Validate const_cast of next line
	av_image_copy(data, linesize, const_cast<const uint8_t**>(copy.data), copy.linesize, format, width, height);

	return *this;
}

TextureSoftware& TextureSoftware::operator=(TextureSoftware&& move) noexcept {
	av_free(data[0]);

	width = move.width;
	height = move.height;
	format = move.format;
	std::copy(move.linesize, move.linesize + 4, linesize);
	std::copy(move.data, move.data + 4, data);

	std::fill(move.data, move.data + 4, nullptr);
	return *this;
}