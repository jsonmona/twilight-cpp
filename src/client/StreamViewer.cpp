#include "StreamViewer.h"


#include <packet.pb.h>
#include <algorithm>

extern "C" {
	#include <libavutil/avutil.h>
}


StreamViewer::StreamViewer() : QOpenGLWidget() {
	log = createNamedLogger("StreamViewer");
	tex = 0;

	decoder = std::make_unique<DecoderSoftware>();
	decoder->setOnFrameAvailable([this](const TextureSoftware& frame) { _onNewFrame(frame); });
	decoder->start();

	setMouseTracking(true);
	connect(this, &StreamViewer::requestRepaint, this, &StreamViewer::executeRepaint);
}

StreamViewer::~StreamViewer() {
	makeCurrent();

	glDeleteTextures(1, &tex);
	glDeleteBuffers(1, &quadBuffer);

	doneCurrent();
}

void StreamViewer::mouseMoveEvent(QMouseEvent* ev) {
	QOpenGLWidget::mouseMoveEvent(ev);
}

void StreamViewer::executeRepaint() {
	repaint();
}

void StreamViewer::initializeGL() {
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
	texcoord.y = 1.0f - texcoord.y;
	v_texcoord = texcoord;
})STR";
	static const char fragShaderCode[] = R"STR(
varying vec2 v_texcoord;
uniform sampler2D sampler;
void main() {
	gl_FragColor = texture2D(sampler, v_texcoord);
})STR";
	static const float quad[] = {
		1, 1,
		-1, 1,
		-1, -1,
		1, -1,
	};

	initializeOpenGLFunctions();

	log->info("GL vendor: {}", glGetString(GL_VENDOR));
	log->info("GL renderer: {}", glGetString(GL_RENDERER));
	log->info("GL version: {}", glGetString(GL_VERSION));

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
	if(!flagCompiled) {
		GLint logSize = 0;
		glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &logSize);
		std::vector<GLchar> errorLog(logSize);
		glGetShaderInfoLog(vertexShader, logSize, &logSize, errorLog.data());
		
		log->error("Failed to compile shader.\nCompiler log:\n{}", errorLog.data());
		error_quit(log, "Failed to compile vertex shader");
	}

	code = fragShaderCode;
	len = sizeof(fragShaderCode);
	fragShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragShader, 1, &code, &len);
	glCompileShader(fragShader);

	flagCompiled = 0;
	glGetShaderiv(fragShader, GL_COMPILE_STATUS, &flagCompiled);
	if (!flagCompiled) {
		GLint logSize = 0;
		glGetShaderiv(fragShader, GL_INFO_LOG_LENGTH, &logSize);
		std::vector<GLchar> errorLog(logSize);
		glGetShaderInfoLog(fragShader, logSize, &logSize, errorLog.data());

		log->error("Failed to compile fragment shader.\nCompiler log:\n{}", errorLog.data());
		error_quit(log, "Failed to compile fragment shader");
	}

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
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	/* update desktopImage */ {
		uint8_t* nowImage = desktopImage.exchange(nullptr, std::memory_order_seq_cst);

		if (nowImage != nullptr) {
			hasTexture = true;

			glBindTexture(GL_TEXTURE_2D, tex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nowImage);

			free(nowImage);
		}
	}

	/* update cursorImage */ {
		uint8_t* nowImage = cursorImage.exchange(nullptr, std::memory_order_seq_cst);

		if (nowImage != nullptr) {
			glBindTexture(GL_TEXTURE_2D, cursorTex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cursorWidth, cursorHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nowImage);

			free(nowImage);
		}
	}

	if (!hasTexture)
		return;

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

void StreamViewer::onNewPacket(const msg::Packet& pkt, uint8_t* extraData) {
	switch (pkt.msg_case()) {
	case msg::Packet::kDesktopFrame: {
		cursorVisible = pkt.desktop_frame().cursor_visible();
		cursorX = pkt.desktop_frame().cursor_x();
		cursorY = pkt.desktop_frame().cursor_y();

		decoder->pushData(extraData, pkt.extra_data_len());
		break;
	}
	case msg::Packet::kCursorShape: {
		cursorWidth = pkt.cursor_shape().width();
		cursorHeight = pkt.cursor_shape().height();

		if (cursorWidth * cursorHeight * 4 != pkt.extra_data_len())
			error_quit(log, "Invalid length of extra data (wire format mismatch)");

		uint8_t* nowImage = reinterpret_cast<uint8_t*>(malloc(cursorWidth * cursorHeight * 4));
		check_quit(nowImage == nullptr, log, "Failed to allocate cursor image");
		memcpy(nowImage, extraData, pkt.extra_data_len());
		free(cursorImage.exchange(nowImage, std::memory_order_seq_cst));
		break;
	}
	default:
		log->error("Unknown packet msg_case: {}", pkt.msg_case());
	}

	requestRepaint();
}

void StreamViewer::_onNewFrame(const TextureSoftware& frame) {
	uint8_t* nowImage = reinterpret_cast<uint8_t*>(malloc(1920 * 1080 * 4));
	check_quit(nowImage == nullptr, log, "Unable to allocate new desktop image");

	this->width = frame.width;
	this->height = frame.height;
	for (int i = 0; i < 1080; i++)
		memcpy(nowImage + (1920 * 4 * i), frame.data[0] + (frame.linesize[0] * i), 1920 * 4);

	free(desktopImage.exchange(nowImage, std::memory_order_seq_cst));
}