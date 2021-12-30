#ifndef TWILIGHT_COMMON_PLATFORM_SOFTWARE_SCALESOFTWARE_H
#define TWILIGHT_COMMON_PLATFORM_SOFTWARE_SCALESOFTWARE_H

#include "common/ffmpeg-headers.h"
#include "common/log.h"

#include "TextureSoftware.h"

class ScaleSoftware {
    LoggerPtr log = createNamedLogger("ScaleSoftware");
    bool hasTexture = false;
    bool formatChanged = false;
    bool dirty = false;

    int inputWidth = -1, inputHeight = -1;
    AVPixelFormat inputFormat = AV_PIX_FMT_NONE;

    int outputWidth = -1, outputHeight = -1;
    AVPixelFormat outputFormat = AV_PIX_FMT_NONE;

    TextureSoftware inputTex, outputTex;

    SwsContext *ctx = nullptr;

    void _init();
    void _convert();

public:
    ScaleSoftware() {}
    ~ScaleSoftware() noexcept;

    void reset();

    void setInputFormat(int w, int h, AVPixelFormat fmt);
    void setOutputFormat(int w, int h, AVPixelFormat fmt);

    void pushInput(TextureSoftware &&tex);
    TextureSoftware popOutput();
    TextureSoftware &&moveOutput();

    void flush();
};

#endif