#ifndef CLIENT_STREAM_VIEWER_H_
#define CLIENT_STREAM_VIEWER_H_


#include "common/log.h"

#include <QtOpenGLWidgets/qopenglwidget.h>
#include <QtGui/qopenglfunctions.h>
#include <QtCore/qtimer.h>

#include <chrono>


struct AVCodec;
struct AVCodecContext;

class StreamViewer : public QOpenGLWidget, protected QOpenGLFunctions {
	Q_OBJECT;
	GLuint quadBuffer, quadArray;
	GLuint tex, cursorTex;
	GLuint vertexShader, fragShader, program;
	GLuint posAttrib, rectUniform;

	std::chrono::steady_clock::time_point lastUpdate;
	bool hasTexture = false;
	bool cursorVisible = false;
	int cursorX, cursorY, cursorWidth, cursorHeight;
	int width, height;

	LoggerPtr log;

	AVCodec* decoder;
	AVCodecContext* decoderContext;

	QTimer timer;

public slots:
	void executeUpdate();

public:
	std::vector<uint8_t> data;
	int dataPos;

	StreamViewer();
	~StreamViewer() override;

	void mouseMoveEvent(QMouseEvent* ev) override;

	void initializeGL() override;
	void resizeGL(int w, int h) override;
	void paintGL();
};


#endif