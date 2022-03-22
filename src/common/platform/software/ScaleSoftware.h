#ifndef TWILIGHT_COMMON_PLATFORM_SOFTWARE_SCALESOFTWARE_H
#define TWILIGHT_COMMON_PLATFORM_SOFTWARE_SCALESOFTWARE_H

#include "common/Rational.h"
#include "common/ffmpeg-headers.h"
#include "common/log.h"

#include "TextureSoftware.h"

class ScaleSoftware {
public:
    ScaleSoftware();
    ~ScaleSoftware() noexcept;

    void reset();

    void setInputFormat(int w, int h, AVPixelFormat fmt);
    void setOutputFormat(int w, int h, AVPixelFormat fmt);

    void getRatio(Rational *xRatio, Rational *yRatio);

    void pushInput(TextureSoftware &&tex);
    TextureSoftware popOutput();

    void flush();

private:
    void convert_();

    LoggerPtr log;
    std::shared_ptr<TextureAllocArena> outputArena;

    bool hasTexture;
    bool inputFormatChanged;
    bool outputFormatChanged;
    bool dirty;

    int inputWidth, inputHeight;
    AVPixelFormat inputFormat;

    int outputWidth, outputHeight;
    AVPixelFormat outputFormat;

    TextureSoftware inputTex, outputTex;

    SwsContext *ctx;
};

#endif
