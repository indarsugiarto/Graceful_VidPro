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
#include "cImgViewer.h"
#include "cPixViewer.h"


// For GUI testing, we don't need SpiNNaker:
// #define BYPASS_SPIN

// fps can be computed from decoder output or spinnaker output
#ifndef BYPASS_SPIN
#define USE_SPIN_OUTPUT_FOR_FPS
#endif


#define USE_QGRAPHICSVIEW	1
#define USE_QGLWIDGET		2	// problematic with vga-size video
#define USE_PIXMAP			3	// runs OK with BYPASS_SPIN
#define RENDERING_ENGINE	USE_PIXMAP

#define DELAY_SIMULT_BUFFERING	(450*3)

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
	QTimer *avgfpsT;

#if(RENDERING_ENGINE==USE_QGRAPHICSVIEW)	// it has scrollbar functionality
	cScreen *edge;
	cScreen *orgImg;
#elif(RENDERING_ENGINE==USE_QGLWIDGET)
	cImgViewer *edge;
	cImgViewer *orgImg;
#else
	cPixViewer *edge;
	cPixViewer *orgImg;
#endif

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
    void newChipsNum();
    void refreshUpdate();
	void videoFinish();
	void setSize(int w, int h);
	void cbSpiNNchanged(int idx);
	void frameReady();
	void frameSent();
	void spinnSendFrame();
	void edgeRenderingDone();		// useful only for video, not image processing
	void screenRenderingDone();
	void cleanUpVideo();
	void changeGovernor(int idx);

	void pbAnimClicked();
	void exFPSChanged(int val);
	quint64 getElapse_ns();
	double get_fps();
	void avgfpsT_tick();
	void updateSpinConnection();
	void runAnimation();
	void updateSimultDelFactor(int newVal) {_iSimultDelFactor = newVal;}

private:
    bool isPaused;
	bool edgeRenderingInProgress;
	bool decoderIsActive;

	int _iSimultDelFactor;
	int tictoc;
	timespec tic, toc;

	QString currDir;

	volatile bool _bAnimIsRunning;
	bool _bEdgeRenderingDone;
	bool _bRefresherUpdated;
	bool _spinIsReady;	// will on when spin send result
	int _exFPS;
	int nRecvFrame;
	Ui::vidStreamer *ui;

	int simultDelVal;
	quint64 elapsed(timespec start, timespec end);
	void giveDelay(quint32 ns);

protected:
	void closeEvent(QCloseEvent *event);
};

#endif // VIDSTREAMER_H
