#include <QtGui>
#include <QtOpenGL>
#include "cImgViewer.h"

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE  0x809D
#endif

cImgViewer::cImgViewer(QString title, QWidget *parent)
	: QGLWidget(QGLFormat(QGL::SampleBuffers), parent)
{
	setWindowTitle(title);
}

void cImgViewer::initializeGL()
{
	QColor black(0,0,0);
	qglClearColor(black);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glShadeModel(GL_SMOOTH);
	glEnable(GL_MULTISAMPLE);
}

void cImgViewer::putFrame(const QImage &frame)
{
	//_frame = frame;
	_glFrame = QGLWidget::convertToGLFormat(frame);

	//resize(frame.size());
	updateGL();
}

void cImgViewer::setSize(int w, int h)
{
	imgW = w;
	imgH = h;
	this->resize(w, h);
}

void cImgViewer::resizeGL(int width, int height)
{
	glViewport(0, 0, width, height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	//glOrtho(0, w,0,h,-1,1);
#ifdef QT_OPENGL_ES_1
	glOrthof(-0.5, +0.5, -0.5, +0.5, 4.0, 15.0);
#else
	glOrtho(-0.5, +0.5, -0.5, +0.5, 4.0, 15.0);
#endif
	glMatrixMode(GL_MODELVIEW);
}

void cImgViewer::paintGL()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glLoadIdentity();
	// then draw the image:
	glDrawPixels(imgW, imgH, GL_RGBA, GL_UNSIGNED_BYTE, _glFrame.bits());
	emit renderDone();
}
