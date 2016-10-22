#ifndef CSCREEN_H
#define CSCREEN_H

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPixmap>
#include <QImage>

class cScreen : public QWidget
{
	Q_OBJECT
public:
	explicit cScreen(QString title, QWidget *parent = 0);
	~cScreen();
	QGraphicsView *viewPort;
	QGraphicsScene *scene;
	QImage frame;
signals:
	void renderDone();
public slots:
	void setSize(int w, int h);
	void putFrame(const QImage &frame);
	//void drawFrame();
	void clear();
private:
	int imgW;
	int imgH;
	int screenW, screenH;
};

#endif // CSCREEN_H


