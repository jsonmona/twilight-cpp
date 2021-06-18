#ifndef CLIENT_STREAM_VIEWER_H_
#define CLIENT_STREAM_VIEWER_H_


#include "common/log.h"

#include <QtOpenGLWidgets/qopenglwidget.h>
#include <QtGui/qopenglfunctions.h>

#include <cstdio>


struct AVCodec;
struct AVCodecContext;

class StreamViewer : public QOpenGLWidget, protected QOpenGLFunctions {
	Q_OBJECT;
	GLuint tex, quadBuffer, quadArray;
	GLuint vertexShader, fragShader, program;
	GLuint posAttrib;

	bool hasTexture = false;

	LoggerPtr log;

	FILE* f;
	AVCodec* decoder;
	AVCodecContext* decoderContext;

public:
	StreamViewer();
	~StreamViewer() override;

	void mouseMoveEvent(QMouseEvent* ev) override;

	void initializeGL() override;
	void resizeGL(int w, int h) override;
	void paintGL();
};


#endif