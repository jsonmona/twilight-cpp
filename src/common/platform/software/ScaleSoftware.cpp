#include "ScaleSoftware.h"

ScaleSoftware::ScaleSoftware()
    : log(createNamedLogger("ScaleSoftware")),
      hasTexture(false),
      inputFormatChanged(false),
      outputFormatChanged(false),
      dirty(false),
      inputWidth(-1),
      inputHeight(-1),
      inputFormat(AV_PIX_FMT_NONE),
      outputWidth(-1),
      outputHeight(-1),
      outputFormat(AV_PIX_FMT_NONE),
      ctx(nullptr) {}

ScaleSoftware::~ScaleSoftware() {
    sws_freeContext(ctx);
}

void ScaleSoftware::getRatio(Rational* xRatio, Rational* yRatio) {
    *xRatio = {outputWidth, inputWidth};
    *yRatio = {outputHeight, inputHeight};
}

void ScaleSoftware::reset() {
    hasTexture = false;
    dirty = false;
}

void ScaleSoftware::setInputFormat(int w, int h, AVPixelFormat fmt) {
    if (fmt != inputFormat || w != inputWidth || h != inputHeight) {
        inputWidth = w;
        inputHeight = h;
        inputFormat = fmt;
        inputFormatChanged = true;
    }
}

void ScaleSoftware::setOutputFormat(int w, int h, AVPixelFormat fmt) {
    if (fmt != outputFormat || w != outputWidth || h != outputHeight) {
        outputWidth = w;
        outputHeight = h;
        outputFormat = fmt;
        outputFormatChanged = true;
    }
}

void ScaleSoftware::pushInput(TextureSoftware&& tex) {
    inputTex = std::move(tex);

    hasTexture = true;
    dirty = true;
}

TextureSoftware ScaleSoftware::popOutput() {
    check_quit(!hasTexture, log, "Tried to pop output when empty");

    if (dirty)
        convert_();

    return outputTex.clone(outputArena);
}

void ScaleSoftware::flush() {
    if (dirty)
        convert_();
}

void ScaleSoftware::convert_() {
    dirty = false;
    setInputFormat(inputTex.width, inputTex.height, inputTex.format);

    if (inputFormatChanged || outputFormatChanged) {
        sws_freeContext(ctx);

        ctx = sws_getContext(inputWidth, inputHeight, inputFormat, outputWidth, outputHeight, outputFormat,
                             SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    }
    if (outputFormatChanged) {
        ensureFormat(&outputArena, outputWidth, outputHeight, outputFormat);
        outputTex = outputArena->alloc();
    }

    inputFormatChanged = outputFormatChanged = false;

    sws_scale(ctx, inputTex.data, inputTex.linesize, 0, inputHeight, outputTex.data, outputTex.linesize);
}
