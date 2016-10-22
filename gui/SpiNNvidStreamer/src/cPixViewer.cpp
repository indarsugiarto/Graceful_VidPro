#include "cPixViewer.h"
#include <QDebug>

cPixViewer::cPixViewer(QString title, QWidget *parent) : QWidget(parent)
{
	ttl = title;
	img = new QLabel(this);
	setWindowTitle(title);
}

void cPixViewer::setSize(int width, int height)
{
	wImg = width;
	hImg = height;
	img->resize(width, height);
	this->resize(width, height);
	qDebug() << QString("[%1] Frame size %2 x %3").arg(ttl).arg(wImg).arg(hImg);
}

void cPixViewer::putFrame(const QImage &frame)
{
	//qDebug() << QString("[%1] Got frame %2 x %3").arg(ttl).arg(frame.width()).arg(frame.height());
	img->setPixmap(QPixmap::fromImage(frame));
	emit renderDone();
}
