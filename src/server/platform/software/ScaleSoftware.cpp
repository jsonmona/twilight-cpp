#include "ScaleSoftware.h"


ScaleSoftware::ScaleSoftware(int w, int h, ScaleType t) :
	log(createNamedLogger("ScaleSoftware")),
	outputWidth(w), outputHeight(h),
	inputFormat(AV_PIX_FMT_NONE), inputWidth(-1), inputHeight(-1),
	initialized(false), dirty(false), ctx(nullptr)
{
	switch (t) {
	case ScaleType::AYUV:
		outputFormat = AV_PIX_FMT_YUV444P;
		break;
	case ScaleType::NV12:
		outputFormat = AV_PIX_FMT_YUV420P;
		break;
	default:
		error_quit(log, "Unsupported ScaleType: {}", static_cast<int>(t));
	}

	outputTex = TextureSoftware(outputWidth, outputHeight, outputFormat);
}

ScaleSoftware::~ScaleSoftware() {
	sws_freeContext(ctx);
}

void ScaleSoftware::reset() {
	initialized = false;
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

	return outputTex;
}

void ScaleSoftware::_init() {
	sws_freeContext(ctx);

	inputWidth = inputTex.width;
	inputHeight = inputTex.height;
	inputFormat = inputTex.format;

	ctx = sws_getContext(inputWidth, inputHeight, inputFormat,
		outputWidth, outputHeight, outputFormat,
		SWS_BICUBIC, nullptr, nullptr, nullptr);
}

void ScaleSoftware::_convert() {
	if (inputTex.format != inputFormat ||
		inputTex.width != inputWidth ||
		inputTex.height != inputHeight)
		_init();

	sws_scale(ctx, inputTex.data, inputTex.linesize, 0, inputHeight, outputTex.data, outputTex.linesize);
}
