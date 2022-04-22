#include "DecodePipelineSoftD3D.h"

TWILIGHT_DEFINE_LOGGER(DecodePipelineSoftD3D);

DecodePipelineSoftD3D::DecodePipelineSoftD3D(std::unique_ptr<IDecoderSoftware> decoder) : decoder(std::move(decoder)) {
    device = dxgiHelper.createDevice(nullptr, false);
    device->GetImmediateContext(context.data());

    uploader.init(dxgiHelper, device);
}

DecodePipelineSoftD3D::~DecodePipelineSoftD3D() {}

void DecodePipelineSoftD3D::setInputResolution(int width, int height) {
    decoder->setVideoResolution(width, height);
}

void DecodePipelineSoftD3D::setOutputResolution(int width, int height) {
    scale.setOutputFormat(width, height, AV_PIX_FMT_RGBA);
}

void DecodePipelineSoftD3D::pushData(DesktopFrame<ByteBuffer>&& frame) {
    decoder->pushData(std::move(frame));
}

bool DecodePipelineSoftD3D::render(RendererD3D* renderer, DesktopFrame<D3D11Texture2D>* frame) {
    if (!readD3D_(frame))
        return false;

    renderer->render(*frame);
    return true;
}

void DecodePipelineSoftD3D::start() {
    decoder->start();
}

void DecodePipelineSoftD3D::stop() {
    decoder->stop();
}

bool DecodePipelineSoftD3D::readD3D_(DesktopFrame<D3D11Texture2D>* output) {
    DesktopFrame<TextureSoftware> soft;
    if (!decoder->readSoftware(&soft))
        return false;

    scale.pushInput(std::move(soft.desktop));
    *output = soft.getOtherType(uploader.upload(scale.popOutput()));
    return true;
}
