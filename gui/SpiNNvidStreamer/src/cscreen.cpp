#include "cscreen.h"
#include <QVBoxLayout>
#include <QDebug>
#include <QtGui>
#include <QtCore>
#include <QGLWidget>

cScreen::cScreen(QString title, QWidget *parent) : QWidget(parent)
{

	 QRect rec = QApplication::desktop()->screenGeometry();
	 screenH = rec.height();
	 screenW = rec.width();

	//QApplication is defined in QtGui
	//QRect is defined in QtCore


	QVBoxLayout layout;
	viewPort = new QGraphicsView();
	viewPort->setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));
	scene = new QGraphicsScene();

	viewPort->setScene(scene);
	//viewPort->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	//viewPort->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	viewPort->setBackgroundBrush(QBrush(Qt::black));
	layout.addWidget(viewPort);
	this->setLayout(&layout);
	this->setWindowTitle(title);
	// test
	//viewPort->show();
}

cScreen::~cScreen()
{
	delete scene;
	delete viewPort;
}

/*
void cScreen::drawFrame()
{
	QPixmap pixmap;
	pixmap.convertFromImage(frame);
	scene->clear();
	scene->addPixmap(pixmap);
	emit renderDone();
}
*/
void cScreen::putFrame(const QImage &frameku)
{
	frame = frameku;
	//drawFrame();

	QPixmap pixmap;
	pixmap.convertFromImage(frameku);
	scene->clear();
	scene->addPixmap(pixmap);
	emit renderDone();
}

void cScreen::setSize(int w, int h){
	bool resz = false;
	if(h > screenH) {h = screenH; resz = true;}
	if(w > screenW) {w = screenW; resz = true;}
	viewPort->setGeometry(0,0,w,h);
	this->setGeometry(x(), y(), w, h);
	this->show();
	imgW = w;
	imgH = h;
	if(!resz) {
		viewPort->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		viewPort->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	}
	else {
		viewPort->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		viewPort->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	}
}

void cScreen::clear()
{
	scene->clear();
}

