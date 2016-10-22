#ifndef CPIXVIEWER_H
#define CPIXVIEWER_H

#include <QWidget>
#include <QLabel>
#include <QImage>

class cPixViewer : public QWidget
{
	Q_OBJECT
public:
	explicit cPixViewer(QString title, QWidget *parent = 0);

signals:
	void renderDone();
public slots:
	void putFrame(const QImage &frame);
	void setSize(int width, int height);
private:
	QLabel *img;
	int wImg, hImg;
	QString ttl;
};

#endif // CPIXVIEWER_H
