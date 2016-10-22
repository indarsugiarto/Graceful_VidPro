#ifndef CIMGVIEWER_H
#define CIMGVIEWER_H

#include <QObject>
#include <QGLWidget>
#include <QImage>

class cImgViewer : public QGLWidget
{
	Q_OBJECT

public:
	cImgViewer(QString title, QWidget *parent = 0);
	//~cImgViewer(); //to ensure that any OpenGL-specific data structures are deleted
public slots:
	void putFrame(const QImage &frame);
	void setSize(int w, int h);	// is replaced by resizeGL()
signals:
	void renderDone();
private:
	int imgW;
	int imgH;
	//int screenW, screenH;
	QImage _glFrame;
protected:
	void initializeGL();
	void paintGL();
	void resizeGL(int w, int h);
};

#endif // CIMGVIEWER_H
