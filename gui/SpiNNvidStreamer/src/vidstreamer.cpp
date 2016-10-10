#include "vidstreamer.h"
#include "ui_vidstreamer.h"

#include <QDebug>
#include <QMessageBox>
#include <QFileDialog>
#include <QDesktopWidget>	// needed for QApplication::desktop()->screenGeometry();

vidStreamer::vidStreamer(QWidget *parent) :
    QWidget(parent),
    worker(NULL),
    decoder(NULL),
    isPaused(false),
	edgeRenderingInProgress(false),
	decoderIsActive(false),
	tictoc(0),
	currDir("../../../../Graceful_VidPro_Media/images"),
    ui(new Ui::vidStreamer)
{
    ui->setupUi(this);
    //ui->cbSpiNN->setCurrentIndex(1);

	// at the moment, disable dvs functionality
	ui->rbDVS->setEnabled(false);

	// we also disable setting number of cores:
	ui->sbPixelWorkers->setEnabled(false);

	// disable buttons until spinnaker is configured
	ui->pbVideo->setEnabled(false);
	ui->pbImage->setEnabled(false);
	ui->pbSendImage->setEnabled(false);
	ui->pbPause->setEnabled(false);
	ui->cbTest->setEnabled(false);
	ui->pbTest->setEnabled(false);


    refresh = new QTimer(this);
    screen = new cScreen();     // this is for displaying original frame
	screen->setWindowTitle("Original Image/Frame");
    edge = new cScreen();       // this is for displaying result
	edge->setWindowTitle("Result Image/Frame");
    spinn = new cSpiNNcomm();

	// let's "tile" the widgets
	QRect rec = QApplication::desktop()->screenGeometry();
	//int hei = rec.height();
	int wid = rec.width();
	//qDebug() << QString("w = %1, h = %2").arg(wid).arg(hei);
	edge->setGeometry(edge->x()+(wid/2), edge->y(), edge->width(), edge->height());


	connect(ui->pbVideo, SIGNAL(pressed()), this, SLOT(pbVideoClicked()));
	connect(ui->pbPause, SIGNAL(pressed()), this, SLOT(pbPauseClicked()));
	connect(ui->pbImage, SIGNAL(pressed()), this, SLOT(pbImageClicked()));
	connect(ui->pbSendImage, SIGNAL(pressed()), this, SLOT(pbSendImageClicked()));
	connect(ui->pbConfigure, SIGNAL(pressed()), this, SLOT(pbConfigureClicked()));
	connect(ui->pbAnim, SIGNAL(pressed()), this, SLOT(pbAnimClicked()));

	connect(ui->sbNchips, SIGNAL(editingFinished()), this, SLOT(newChipsNum()));
	connect(ui->pbTest, SIGNAL(pressed()), this, SLOT(pbTestClicked()));
	connect(ui->pbShowHideTest, SIGNAL(pressed()), this, SLOT(pbShowHideTestClicked()));
	connect(spinn, SIGNAL(frameOut(const QImage &)), edge, SLOT(putFrame(const QImage &)));
	connect(spinn, SIGNAL(frameOut(const QImage &)), this, SLOT(spinnSendFrame()));

	connect(spinn, SIGNAL(sendFrameDone()), this, SLOT(frameSent()));
	connect(edge, SIGNAL(renderDone()), this, SLOT(edgeRenderingDone()));
	connect(refresh, SIGNAL(timeout()), this, SLOT(refreshUpdate()));

	//refresh->setInterval(20);   // which produces roughly 50fps
	refresh->setInterval(40);   // which produces roughly 25fps
	//refresh->setInterval(100);   // which produces roughly 10fps
	//refresh->setInterval(1000);   // which produces roughly 1fps
	//refresh->setInterval(500);   // which produces roughly 2fps
	//refresh->setInterval(250);   // which produces roughly 4fps
	//refresh->setInterval(50);   // which produces roughly 20fps
	//refresh->setInterval(2000);   // which produces roughly 0.5fps
	refresh->start();

	// additional setup
	cbSpiNNchanged(ui->cbSpiNN->currentIndex());
	connect(ui->cbSpiNN, SIGNAL(currentIndexChanged(int)), this, SLOT(cbSpiNNchanged(int)));

	oldNchips = 0;	// just for tracking if a new number of nodes is entered

	// let's test

	//ui->pbConfigure->click();
	//pbTestClicked();
	/*
	spinn->setHost(1);	//1=spin5
	spinn->configSpin(1, 350, 350);
	*/

	experiment = 0;

	if(experiment > 0) {
		ui->rbLaplace->setChecked(true);
		ui->cbFreq->setCurrentIndex(1);
		switch(experiment){
		case 1: imgFilename = "../../../images/Elephant-vga.bmp"; break;
		case 2: imgFilename = "../../../images/Elephant-svga.bmp"; break;
		case 3: imgFilename = "../../../images/Elephant-xga.bmp"; break;
		case 4: imgFilename = "../../../images/Elephant-sxga.bmp"; break;
		case 5: imgFilename = "../../../images/Elephant-uxga.bmp"; break;
		case 6: imgFilename = "../../../images/halfMillionCoreComplete-huxga.jpg"; break;
		case 7: imgFilename = "../../../images/spinn-512x512.jpg"; break;
		case 8: imgFilename = "../../../images/spinn-1024x1024.jpg"; break;
		case 9: imgFilename = "../../../images/spinn-1476x1680.jpg"; break;
		case 10: imgFilename = "../../../images/spinn-3936x3936.jpg"; break;
		}

	}

#if(DEBUG_LEVEL>1)
	ui->delFactorHost->setValue(10000);
	ui->delFactorSpin->setValue(250);
#endif

// also for target FPGA, disable the SDP parameters
#if(DESTINATION==DEST_FPGA)
	ui->delFactorHost->setEnabled(false);
	ui->delFactorSpin->setEnabled(false);
	ui->rbNone->setChecked(true);
	ui->rbSobel->setEnabled(false);
	ui->rbLaplace->setEnabled(false);
#endif
}

void vidStreamer::cbSpiNNchanged(int idx)
{
	if(idx==0) {
		ui->sbNchips->setValue(4);
		ui->sbNchips->setEnabled(false);
		ui->ipAddr->setText("192.168.240.253");
	} else {
		ui->sbNchips->setEnabled(true);
		ui->ipAddr->setText("192.168.240.1");
	}

}

void vidStreamer::pbShowHideTestClicked()
{
    int _x,_y,w,h;
    _x = x(); _y = y();
    w = width(); h = height();
    if(h < 600) {
        this->setGeometry(_x,_y,w,600);
        return;
    }
    if(h > 450) {
        this->setGeometry(_x,_y,w,450);
        return;
    }
}

void vidStreamer::pbConfigureClicked()
{
    screen->hide();
    edge->hide();
    ui->pbVideo->setEnabled(true);
    ui->pbImage->setEnabled(true);
    ui->pbSendImage->setEnabled(false);
    //ui->pbPause->setEnabled(true);
    ui->cbTest->setEnabled(true);
    ui->pbTest->setEnabled(true);

    // then send information to spinn
    quint8 spinIdx = ui->cbSpiNN->currentIndex();
    quint8 nNodes = ui->sbNchips->value();

	quint8 opType = 0;
	if(ui->rbSobel->isChecked()) opType = 1;
	else if(ui->rbLaplace->isChecked()) opType = 2;
	else if(ui->rbDVS->isChecked()) opType = 3;

    quint8 wFilter = 0;
    if(ui->rbFilterOn->isChecked()) wFilter = 1;

	quint8 wHist = 0;
    if(ui->rbSharpOn->isChecked()) wHist = 1;

	quint8 freq;
	freq = ui->cbFreq->currentText().toInt();

	quint8 nCorePreProc = ui->sbPixelWorkers->value();

	int delHost = ui->delFactorHost->text().toInt();
	int delSpin = ui->delFactorSpin->text().toInt();


	spinn->configSpin(spinIdx, nNodes, opType, wFilter, wHist, freq, nCorePreProc, delHost, delSpin);

    // just for the experiment: it's boring...
    if(experiment > 0) {
        pbImageClicked();
        pbSendImageClicked();
    }
}

void vidStreamer::pbPauseClicked()
{
	if(isPaused==false) {
		ui->pbPause->setText("Run");
		isPaused = true;
		refresh->stop();
	}
	else {
		ui->pbPause->setText("Pause");
		isPaused = false;
		refresh->start();
	}
}

vidStreamer::~vidStreamer()
{
    delete spinn;
	delete edge;
	delete screen;
    delete ui;
}

void vidStreamer::errorString(QString err)
{
    qDebug() << err;
}

void vidStreamer::pbVideoClicked()
{
    QString fName = QFileDialog::getOpenFileName(this, "Open Video File", "../../../../videos", "*");
    if(fName.isEmpty())
        return;
    ui->pbPause->setEnabled(true);

	worker = new QThread();
	decoder = new cDecoder();

	decoder->moveToThread(worker);
	connect(decoder, SIGNAL(error(QString)), this, SLOT(errorString(QString)));
	connect(worker, SIGNAL(started()), decoder, SLOT(started()));
	connect(decoder, SIGNAL(finished()), worker, SLOT(quit()));
	connect(decoder, SIGNAL(finished()), decoder, SLOT(deleteLater()));
	connect(worker, SIGNAL(finished()), worker, SLOT(deleteLater()));
	connect(decoder, SIGNAL(newFrame(const QImage &)), screen, SLOT(putFrame(const QImage &)));

	// use the following to send the image from decoder to spinnaker
	connect(decoder, SIGNAL(newFrame(const QImage &)), spinn, SLOT(frameIn(const QImage &)));
	connect(decoder, SIGNAL(newFrame(const QImage &)), this, SLOT(frameReady(const QImage &)));

	// for debugging only: send the image from decoder to edge-screener
	// connect(decoder, SIGNAL(newFrame(QImage)), edge, SLOT(putFrame(QImage)));

	connect(decoder, SIGNAL(gotPicSz(int,int)), this, SLOT(setSize(int,int)));
	connect(decoder, SIGNAL(finished()), this, SLOT(videoFinish()));

	decoder->filename = fName;
	worker->start();
	decoderIsActive = true;
}

void vidStreamer::refreshUpdate()
{
	_bRefresherUpdated = true;
	// vidstreamer manage the refreshing to create "centralized" update

	// if edge-widget finish rendering, then send the refresh signal
	// to the video decoder

	// debugging: bypassing edgeRenderingInProgress:
	edgeRenderingInProgress = false;

	timespec temp;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &temp);

#if(DESTINATION==DEST_HOST)

	if(!edgeRenderingInProgress && decoderIsActive) {
		qDebug() << QString("Refresh encoder at-%1").arg(temp.tv_sec);
		decoder->refresh();
	}
#else
	if(decoderIsActive) {
		qDebug() << QString("Refresh encoder at-%1").arg(temp.tv_sec);
		decoder->refresh();
	}
#endif

}

void vidStreamer::setSize(int w, int h)
{
	screen->setSize(w,h);
	edge->setSize(w,h);
    // then tell spinnaker to start initialization
    spinn->frameInfo(w, h);
}

void vidStreamer::closeEvent(QCloseEvent *event)
{
	refresh->stop();
    if(worker != NULL && worker->isRunning())
        worker->quit();
	screen->close();
	edge->close();
	event->accept();
}

void vidStreamer::videoFinish()
{
    if(worker != NULL && worker->isRunning())
        worker->quit();
	decoderIsActive = false;
	refresh->stop();
	//screen->hide();
	//edge->hide();
}

void vidStreamer::pbImageClicked()
{
	timespec start, end, temp;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end);
	// compute diff
	if ((end.tv_nsec-start.tv_nsec)<0) {
		temp.tv_sec = end.tv_sec-start.tv_sec-1;
		temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec-start.tv_sec;
		temp.tv_nsec = end.tv_nsec-start.tv_nsec;
	}
	qDebug() << QString("Dummy Elapsed = %1-ns").arg(temp.tv_sec*1000000000+temp.tv_nsec);

    /* Test-1: Send QImage-frame to SpiNNaker and display the result */
    if(experiment == 0) {
        imgFilename = QFileDialog::getOpenFileName(this, "Open Image File", currDir, "*");
        if(imgFilename.isEmpty())
            return;
    }
    currDir = QFileInfo(imgFilename).path(); // store path for next time

	if(experiment == 0)
		screen->show();

    ui->pbSendImage->setEnabled(true);

    loadedImage.load(imgFilename);

	// send image size to widgets and SpiNNaker:
	setSize(loadedImage.width(), loadedImage.height());
	// the widgets will be shown up!!!

	// for the "screen" widget, display the image immediately:
	screen->putFrame(loadedImage);
	screen->hide(); screen->show();	// this is a trick to make the widget properly showing the image
}

void vidStreamer::pbSendImageClicked()
{
	/*
    if(experiment == 0)
        edge->show();
	*/
    // disable the button to see if the image is sent
    ui->pbSendImage->setEnabled(false);
	edge->clear();

    // send the frame to spinn
	frameReady(loadedImage);
    spinn->frameIn(loadedImage);
}

// frameReady() is called when the decoder has a new frame ready to be sent to spinn
void vidStreamer::frameReady(const QImage &frameIn)
{
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tic);

	// first, indicate that the "edge" widget is going to do the rendering
	// so the the refresh signal from the timer will be ignored in refreshUpdate()
	edgeRenderingInProgress = true;

	// for DEBUGGING: the following directly-to-edge-widget works:
	// edge->putFrame(frameIn);

	//loadedImage = frameIn;
	//spinn->frameIn(frameIn);

#if(DEBUG_LEVEL>1)
	qDebug() << "Got new frame from decoder...";
#endif
}

void vidStreamer::pbTestClicked()
{
    spinn->sendTest(ui->cbTest->currentIndex());
}

void vidStreamer::newChipsNum()
{
    if(ui->sbNchips->value() != oldNchips) {
        oldNchips = ui->sbNchips->value();
		//the following is just used during experimentation for the paper:
		//pbConfigureClicked();
    }
}

void vidStreamer::frameSent()
{

    // if using "Load Image"
	/*
    if(!loadedImage.isNull())
        ui->pbSendImage->setEnabled(true);
		*/
#if(DEBUG_LEVEL>1)
	qDebug() << "Frame is sent to spinn...";
#endif
}

void vidStreamer::edgeRenderingDone()
{
	_bEdgeRenderingDone = true;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &toc);
	timespec temp;
	quint64 df_ns;
	double fps;
	if ((toc.tv_nsec-tic.tv_nsec)<0) {
		temp.tv_sec = toc.tv_sec-tic.tv_sec-1;
		temp.tv_nsec = 1000000000+toc.tv_nsec-tic.tv_nsec;
	} else {
		temp.tv_sec = toc.tv_sec-tic.tv_sec;
		temp.tv_nsec = toc.tv_nsec-tic.tv_nsec;
	}
	// difference in nanosecond
	df_ns = temp.tv_sec*1000000000+temp.tv_nsec;
	fps = 1000000000.0/(double)df_ns;
	qDebug() << QString("fps = %1").arg((int)round(fps));


	// indicate that the "edge" widget is finish in rendering a frame
	if(!ui->pbSendImage->isEnabled())
		ui->pbSendImage->setEnabled(true);
	edgeRenderingInProgress = false;
#if(DEBUG_LEVEL>1)
	qDebug() << "Edge rendering done...";
#endif
}

void vidStreamer::spinnSendFrame()
{
#if(DEBUG_LEVEL>1)
	qDebug() << "SpiNNVid send the frame";
#endif
}

void vidStreamer::pbAnimClicked()
{
	QString dir = QFileDialog::getExistingDirectory(this,
													"Open Anim Folder", currDir,
													QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
	if(dir.isNull() || dir.isEmpty()) return;
	currDir = dir;
	QString fName;
	int i=1;

	fName = QString("%1/%2.jpg").arg(dir).arg(i);
	loadedImage.load(fName);
	setSize(loadedImage.width(), loadedImage.height());
	screen->putFrame(loadedImage);

	for(i=2; i<=99; i++) {
		//edge->clear();
		_bEdgeRenderingDone = false;
		_bRefresherUpdated = false;
		//edge->putFrame(loadedImage);
		spinn->frameIn(loadedImage);
		while (1) {
			qApp->processEvents();
			if(_bEdgeRenderingDone && _bRefresherUpdated) break;
			//if(_bEdgeRenderingDone) break;
		}
		fName = QString("%1/%2.jpg").arg(dir).arg(i);
		loadedImage.load(fName);
		screen->putFrame(loadedImage);
	}
}

