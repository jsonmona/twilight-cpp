#ifndef SERVER_CAPTURE_DATA_H_
#define SERVER_CAPTURE_DATA_H_


#include <cstdint>
#include <vector>
#include <memory>


enum class ScaleType {
	AYUV,
	NV12
};

struct CursorData;
struct CursorShapeData;

// TODO: All good except pts(=desktop) is dynamically allocated.
//       Somehow make that better. (Perhaps a separate EncoderData?)
template<class T>
struct CaptureData {
	std::shared_ptr<T> desktop;
	std::shared_ptr<CursorData> cursor;
	std::shared_ptr<CursorShapeData> cursorShape;
};


struct CursorData {
	bool visible;
	int posX, posY;
};

struct CursorShapeData {
	int width, height;
	int hotspotX, hotspotY;
	std::vector<uint8_t> image;
};


#endif