#include "ScaleSoftware.h"


ScaleSoftware::~ScaleSoftware() {
	sws_freeContext(ctx);
}

void ScaleSoftware::reset() {
	initialized = false;
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

void ScaleSoftware::pushInput(TextureSoftware&& tex) {
	inputTex = std::move(tex);

	initialized = true;
	dirty = true;
}

TextureSoftware ScaleSoftware::popOutput() {
	check_quit(!initialized, log, "Used without initialization");

	if (dirty)
		_convert();

	return outputTex.clone();
}

void ScaleSoftware::_init() {
	sws_freeContext(ctx);

	outputTex = TextureSoftware::allocate(outputWidth, outputHeight, outputFormat);

	ctx = sws_getContext(inputWidth, inputHeight, inputFormat,
		outputWidth, outputHeight, outputFormat,
		SWS_BICUBIC, nullptr, nullptr, nullptr);
}

void ScaleSoftware::_convert() {
	setInputFormat(inputTex.width, inputTex.height, inputTex.format);

	if (formatChanged)
		_init();

	sws_scale(ctx, inputTex.data, inputTex.linesize, 0, inputHeight, outputTex.data, outputTex.linesize);
}
