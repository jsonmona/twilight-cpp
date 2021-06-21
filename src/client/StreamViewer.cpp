#include "StreamViewer.h"


#include <packet.pb.h>
#include <algorithm>


extern "C" {
	#include <libavutil/avutil.h>
	#include <libswscale/swscale.h>
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
};


StreamViewer::StreamViewer() : QOpenGLWidget() {
	log = createNamedLogger("decoder");
	tex = 0;

	setMouseTracking(true);
	timer.setInterval(16);
	connect(&timer, &QTimer::timeout, this, &StreamViewer::executeUpdate);
	timer.start();

	decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
	check_quit(decoder == nullptr, log, "Failed to find H264 decoder");

	decoderContext = avcodec_alloc_context3(decoder);
	check_quit(decoderContext == nullptr, log, "Failed to create decoder context");

	int ret = avcodec_open2(decoderContext, decoder, nullptr);
	check_quit(ret < 0, log, "Failed to open decoder context");
}

StreamViewer::~StreamViewer() {
	avcodec_free_context(&decoderContext);

	glDeleteTextures(1, &tex);
	glDeleteBuffers(1, &quadBuffer);
}

void StreamViewer::executeUpdate() {
	update();
}

void StreamViewer::mouseMoveEvent(QMouseEvent* ev) {
	QOpenGLWidget::mouseMoveEvent(ev);
}

void StreamViewer::initializeGL() {
	static const float quad[] = {
		1, 1,
		-1, 1,
		-1, -1,
		1, -1,
	};
	static const char vertexShaderCode[] = R"STR(
uniform vec4 u_rect;
attribute vec2 a_pos;
varying vec2 v_texcoord;
void main() {
	vec2 real_pos = a_pos * 0.5f + 0.5f;
	real_pos *= u_rect.zw;
	real_pos += u_rect.xy;
	real_pos = real_pos * 2.0f - 1.0f;
	gl_Position = vec4(real_pos, 0.0f, 1.0f);
	vec2 texcoord = a_pos * 0.5f + 0.5f;
	texcoord.y = 1 - texcoord.y;
	v_texcoord = texcoord;
})STR";
	static const char fragShaderCode[] = R"STR(
varying vec2 v_texcoord;
uniform sampler2D sampler;
void main() {
	gl_FragColor = texture(sampler, v_texcoord);
})STR";

	initializeOpenGLFunctions();

	glGenBuffers(1, &quadBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, quadBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glGenTextures(1, &cursorTex);
	glBindTexture(GL_TEXTURE_2D, cursorTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	const char* code = vertexShaderCode;
	int len = sizeof(vertexShaderCode);
	vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &code, &len);
	glCompileShader(vertexShader);

	int flagCompiled = 0;
	glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &flagCompiled);
	check_quit(!flagCompiled, log, "Failed to compile vertex shader");

	code = fragShaderCode;
	len = sizeof(fragShaderCode);
	fragShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragShader, 1, &code, &len);
	glCompileShader(fragShader);

	flagCompiled = 0;
	glGetShaderiv(fragShader, GL_COMPILE_STATUS, &flagCompiled);
	check_quit(!flagCompiled, log, "Failed to compile fragment shader");

	program = glCreateProgram();
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragShader);
	glLinkProgram(program);

	posAttrib = glGetAttribLocation(program, "a_pos");
	rectUniform = glGetUniformLocation(program, "u_rect");
}

void StreamViewer::resizeGL(int w, int h) {
	glViewport(0, 0, w, h);
}

void StreamViewer::paintGL() {
	if (!hasTexture || (std::chrono::steady_clock::now() - lastUpdate).count() >= 16'666'666) {
		hasTexture = true;
		lastUpdate = std::chrono::steady_clock::now();

		bool receivedFrame = false;
		AVFrame* frame = av_frame_alloc();

		while (dataPos < data.size()) {
			int ret = avcodec_receive_frame(decoderContext, frame);
			if (ret == AVERROR(EAGAIN)) {
				int readSize;
				uint8_t* frameData = nullptr;

				while (frameData == nullptr && dataPos < data.size()) {
					msg::Packet pck;
					int32_t* sizePtr = reinterpret_cast<int32_t*>(data.data() + dataPos);
					dataPos += 4;
					pck.ParseFromArray(data.data() + dataPos, *sizePtr);
					dataPos += *sizePtr;

					switch (pck.msg_case()) {
					case msg::Packet::kCursorShape:
						cursorWidth = pck.cursor_shape().width();
						cursorHeight = pck.cursor_shape().height();

						glBindTexture(GL_TEXTURE_2D, cursorTex);
						glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cursorWidth, cursorHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
							data.data() + dataPos);

						dataPos += pck.cursor_shape().image_len();
						break;
					case msg::Packet::kDesktopFrame:
						cursorVisible = pck.desktop_frame().cursor_visible();
						cursorX = pck.desktop_frame().cursor_x();
						cursorY = pck.desktop_frame().cursor_y();

						readSize = pck.desktop_frame().image_len();
						frameData = (uint8_t*) av_malloc(readSize + 64);
						check_quit(frameData == nullptr, log, "Failed to allocate data size");
						memcpy(frameData, data.data() + dataPos, readSize);
						dataPos += readSize;
						break;
					default:
						log->warn("Unexpected message type {}", pck.msg_case());
					}
				}

				if (frameData) {
					AVPacket* packet = av_packet_alloc();
					packet->data = frameData;
					packet->size = readSize;
					//packet->flags |= AV_PKT_FLAG_KEY;
					packet->pts = packet->dts = 0;

					ret = avcodec_send_packet(decoderContext, packet);
					av_free(frameData);
					av_packet_free(&packet);
				}
			}
			else if (ret < 0)
				error_quit(log, "Unknown error receiving next frame");
			else {
				receivedFrame = true;
				break;
			}
		}

		if (receivedFrame) {
			static SwsContext* ctx = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
				frame->width, frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);

			width = frame->width;
			height = frame->height;
			int stride = frame->width * 4;
			int bufSize = stride * frame->height;
			uint8_t* plane = reinterpret_cast<uint8_t*>(av_malloc(bufSize));

			int ret = sws_scale(ctx, frame->data, frame->linesize, 0, frame->height, &plane, &stride);

			glBindTexture(GL_TEXTURE_2D, tex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame->width, frame->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, plane);

			//sws_freeContext(ctx);
			av_free(plane);
		}
		av_frame_free(&frame);
	}

	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(program);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);

	glBindBuffer(GL_ARRAY_BUFFER, quadBuffer);
	glUniform4f(rectUniform, 0, 0, 1, 1);
	glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(posAttrib);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	if (cursorVisible) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		float relX = cursorX / (float)(width);
		float relY = cursorY / (float)(height);
		float relWidth = cursorWidth / (float)(width);
		float relHeight = cursorHeight / (float)(height);
		glBindTexture(GL_TEXTURE_2D, cursorTex);
		glUniform4f(rectUniform, relX, 1 - relY - relHeight, relWidth, relHeight);
		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

		glDisable(GL_BLEND);
	}
}
