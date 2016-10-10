#ifndef VIDSTREAMER_H
#define VIDSTREAMER_H

#include <QWidget>
#include <QThread>
#include <QTimer>
#include <QCloseEvent>
#include <QImage>
#include <time.h>

#include "cdecoder.h"
#include "cscreen.h"
#include "cspinncomm.h"
#include "defSpiNNVid.h"

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
    QTimer *refresh;
	cScreen *screen;
	cScreen *edge;
    cSpiNNcomm *spinn;
    QImage loadedImage;
    QString imgFilename;
    int oldNchips;
    int experiment;

public slots:
    void errorString(QString err);
    void pbVideoClicked();
	void pbPauseClicked();
	void pbImageClicked();
	void pbSendImageClicked();
    void pbConfigureClicked();
    void pbTestClicked();
    void pbShowHideTestClicked();
    void newChipsNum();
    void refreshUpdate();
	void videoFinish();
	void setSize(int w, int h);
	void cbSpiNNchanged(int idx);
	void frameReady(const QImage &frameIn);
	void frameSent();
	void spinnSendFrame();
	void edgeRenderingDone();		// useful only for video, not image processing
	void pbAnimClicked();

private:
    bool isPaused;
	bool edgeRenderingInProgress;
	bool decoderIsActive;
    Ui::vidStreamer *ui;

	int tictoc;
	timespec tic, toc;

	QString currDir;

	bool _bEdgeRenderingDone;
	bool _bRefresherUpdated;

protected:
	void closeEvent(QCloseEvent *event);
};

#endif // VIDSTREAMER_H
