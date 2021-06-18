#include "StreamViewer.h"


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

	decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
	check_quit(decoder == nullptr, log, "Failed to find H264 decoder");

	decoderContext = avcodec_alloc_context3(decoder);
	check_quit(decoderContext == nullptr, log, "Failed to create decoder context");

	int ret = avcodec_open2(decoderContext, decoder, nullptr);
	check_quit(ret < 0, log, "Failed to open decoder context");

	f = fopen("../server-stream.dump", "rb");
	check_quit(f == nullptr, log, "Failed to open server stream dump");
}

StreamViewer::~StreamViewer() {
	fclose(f);
	avcodec_free_context(&decoderContext);

	glDeleteTextures(1, &tex);
	glDeleteBuffers(1, &quadBuffer);
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
attribute vec2 a_pos;
varying vec2 v_texcoord;
void main() {
	gl_Position = vec4(a_pos, 0.0f, 1.0f);
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
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
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
}

void StreamViewer::resizeGL(int w, int h) {
	glViewport(0, 0, w, h);
}

void StreamViewer::paintGL() {
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(program);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);

	if (!hasTexture) {
		hasTexture = true;
		AVFrame* frame = av_frame_alloc();

		while (true) {
			int ret = avcodec_receive_frame(decoderContext, frame);
			if (ret == AVERROR(EAGAIN)) {
				int_fast32_t packetLen;
				fread(&packetLen, 4, 1, f);

				AVPacket* packet = av_packet_alloc();
				uint8_t* buf = (uint8_t*)av_malloc(std::max(AV_INPUT_BUFFER_MIN_SIZE, packetLen) + AV_INPUT_BUFFER_PADDING_SIZE);

				int readsize = 0;
				while (readsize < packetLen) {
					int result = fread(buf + readsize, 1, packetLen - readsize, f);
					check_quit(result <= 0, log, "Failed to read server stream dump");
					// TODO: flush stream and stop
					readsize += result;
				}

				packet->data = reinterpret_cast<uint8_t*>(buf);
				packet->size = readsize;
				packet->flags |= AV_PKT_FLAG_KEY;
				packet->pts = packet->dts = 0;

				ret = avcodec_send_packet(decoderContext, packet);
				av_free(buf);
				av_packet_free(&packet);
			}
			else if (ret < 0)
				error_quit(log, "Unknown error receiving next frame");
			else
				break;
		}

		SwsContext* ctx = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
			frame->width, frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);

		int stride = frame->width * 4;
		int bufSize = stride * frame->height;
		uint8_t* plane = reinterpret_cast<uint8_t*>(av_malloc(bufSize));

		int ret = sws_scale(ctx, frame->data, frame->linesize, 0, frame->height, &plane, &stride);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame->width, frame->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, plane);

		sws_freeContext(ctx);
		av_frame_free(&frame);

		av_free(plane);
	}
	
	glBindBuffer(GL_ARRAY_BUFFER, quadBuffer);
	glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(posAttrib);

	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}