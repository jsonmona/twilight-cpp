#ifndef SERVER_PLATFORM_SOFTWARE_SCALE_SOFTWARE_H_
#define SERVER_PLATFORM_SOFTWARE_SCALE_SOFTWARE_H_


#include "common/log.h"

#include "server/CaptureData.h"
#include "TextureSoftware.h"

extern "C" {
#include <libswscale/swscale.h>
}


class ScaleSoftware {
	LoggerPtr log;
	bool initialized, dirty;

	int inputWidth, inputHeight;
	AVPixelFormat inputFormat;

	int outputWidth, outputHeight;
	AVPixelFormat outputFormat;

	TextureSoftware inputTex, outputTex;

	SwsContext* ctx;

	void _init();
	void _convert();

public:
	ScaleSoftware(int w, int h, ScaleType t);
	~ScaleSoftware() noexcept;

	void reset();

	void pushInput(TextureSoftware&& tex);
	TextureSoftware popOutput();
};


#endif