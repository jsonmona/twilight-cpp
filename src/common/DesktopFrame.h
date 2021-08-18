#ifndef COMMON_VIDEO_FRAME_H_
#define COMMON_VIDEO_FRAME_H_


#include <cstdint>
#include <vector>
#include <memory>


//TODO: Move this to a more proper place
enum class ScaleType {
	AYUV,
	NV12
};


struct CursorPos;
struct CursorShape;


template<typename T>
struct DesktopFrame {
	std::shared_ptr<T> desktop;
	std::shared_ptr<CursorPos> cursorPos;
	std::shared_ptr<CursorShape> cursorShape;
};


struct CursorPos {
	bool visible;
	int x, y;
};


struct CursorShape {
	int width, height;
	int hotspotX, hotspotY;
	std::vector<uint8_t> image;
};


#endif