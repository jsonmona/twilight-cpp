#include "ScaleSoftware.h"

ScaleSoftware::~ScaleSoftware() {
    sws_freeContext(ctx);
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
        formatChanged = true;
    }
}

void ScaleSoftware::setOutputFormat(int w, int h, AVPixelFormat fmt) {
    if (fmt != outputFormat || w != outputWidth || h != outputHeight) {
        outputWidth = w;
        outputHeight = h;
        outputFormat = fmt;
        formatChanged = true;
    }
}

void ScaleSoftware::pushInput(TextureSoftware &&tex) {
    inputTex = std::move(tex);

    hasTexture = true;
    dirty = true;
}

TextureSoftware ScaleSoftware::popOutput() {
    check_quit(!hasTexture, log, "Tried to pop output when empty");

    if (dirty)
        _convert();

    return outputTex.clone();
}

TextureSoftware &&ScaleSoftware::moveOutput() {
    check_quit(!hasTexture, log, "Tried to move output when empty");
    hasTexture = false;

    if (dirty)
        _convert();

    return std::move(outputTex.clone());
}

void ScaleSoftware::flush() {
    if (dirty)
        _convert();
}

void ScaleSoftware::_init() {
    sws_freeContext(ctx);

    ctx = sws_getContext(inputWidth, inputHeight, inputFormat, outputWidth, outputHeight, outputFormat, SWS_BICUBIC,
                         nullptr, nullptr, nullptr);
}

void ScaleSoftware::_convert() {
    dirty = false;
    setInputFormat(inputTex.width, inputTex.height, inputTex.format);

    if (formatChanged)
        _init();

    if (outputTex.allocated == nullptr)
        outputTex = TextureSoftware::allocate(outputWidth, outputHeight, outputFormat);

    sws_scale(ctx, inputTex.data, inputTex.linesize, 0, inputHeight, outputTex.data, outputTex.linesize);
}
