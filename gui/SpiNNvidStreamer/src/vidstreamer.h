#ifndef VIDSTREAMER_H
#define VIDSTREAMER_H

#include <QWidget>
#include <QThread>
#include <QTimer>
#include <QCloseEvent>

#include "cdecoder.h"
#include "cscreen.h"
#include "cspinncomm.h"

namespace Ui {
class vidStreamer;
}

class vidStreamer : public QWidget
{
    Q_OBJECT

public:
    explicit vidStreamer(QWidget *parent = 0);
    ~vidStreamer();
    QThread *worker;
    cDecoder *decoder;
    VideoState *is;
    QString fName;
    QTimer *refresh;
	cScreen *screen;
	cScreen *edge;
    cSpiNNcomm *spinn;

public slots:
    void errorString(QString err);
    void pbLoadClicked();
	void pbPauseClicked();
    void pbTestClicked();
    void pbConfigureClicked();
    void refreshUpdate();
	void videoFinish();
	void setSize(int w, int h);
	void cbSpiNNchanged(int idx);
	void frameReady();

private:
    bool isPaused;
    Ui::vidStreamer *ui;
protected:
	void closeEvent(QCloseEvent *event);
};

#endif // VIDSTREAMER_H
