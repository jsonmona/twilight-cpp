#ifndef CLIENT_STREAM_VIEWER_H_
#define CLIENT_STREAM_VIEWER_H_


#include "common/log.h"
#include "client/platform/software/DecoderSoftware.h"

#include <packet.pb.h>

#include <QtOpenGLWidgets/qopenglwidget.h>
#include <QtGui/qopenglfunctions.h>


class StreamViewer : public QOpenGLWidget, protected QOpenGLFunctions {
	Q_OBJECT;
	LoggerPtr log;

	GLuint quadBuffer, quadArray;
	GLuint tex, cursorTex;
	GLuint vertexShader, fragShader, program;
	GLuint posAttrib, rectUniform;

	bool hasTexture = false;
	bool cursorVisible = false;

	std::unique_ptr<DecoderSoftware> decoder;
	std::atomic<uint8_t*> desktopImage = nullptr;
	std::atomic<uint8_t*> cursorImage = nullptr;
	int cursorX, cursorY, cursorWidth, cursorHeight;
	int width, height;

	void _onNewFrame(const TextureSoftware& frame);

public slots:
	void executeRepaint();

signals:
	void requestRepaint();

public:
	StreamViewer();
	~StreamViewer() override;

	void onNewPacket(const msg::Packet& pkt, uint8_t* extraData);

	void mouseMoveEvent(QMouseEvent* ev) override;

	void initializeGL() override;
	void resizeGL(int w, int h) override;
	void paintGL();
};


#endif