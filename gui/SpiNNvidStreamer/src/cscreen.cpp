#include "cscreen.h"
#include <QVBoxLayout>
#include <QDebug>

cScreen::cScreen(QWidget *parent) : QWidget(parent)
{
	QVBoxLayout layout;
	viewPort = new QGraphicsView();
	scene = new QGraphicsScene();

	viewPort->setScene(scene);
	viewPort->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	viewPort->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	viewPort->setBackgroundBrush(QBrush(Qt::black));
	layout.addWidget(viewPort);
	this->setLayout(&layout);

	// test
	viewPort->show();
}

cScreen::~cScreen()
{
	delete scene;
	delete viewPort;
}


void cScreen::drawFrame()
{
	QPixmap pixmap;
	pixmap.convertFromImage(frame);
	scene->clear();
	scene->addPixmap(pixmap);
	emit renderDone();
}

void cScreen::putFrame(const QImage &frameku)
{
	frame = frameku;
	drawFrame();
	/*
	QPixmap pixmap;
	pixmap.convertFromImage(frameku);
	scene->clear();
	scene->addPixmap(pixmap);
	*/
}

void cScreen::setSize(int w, int h){
	viewPort->setGeometry(0,0,w,h);
	this->setGeometry(x(), y(), w, h);
	this->show();
	imgW = w;
	imgH = h;
}

void cScreen::getImgSpiNN(const QByteArray &data)
{
	// collect data before constructing QImage from data
}
