/* LOG:
 * - Somehow, the ethernet at Spin3 seems a bit "faster" than Spin5. Hence, we found
 *   the following values are appropriate:
 * 		ui->delFactorHost->setValue(475); for Spin3
 * 		ui->delFactorHost->setValue(550); for Spin5
 * - Also, the newest scamp version (3.xx) seems to work better than 1.34. Less (or no)
 *   MCPL drops...
 * */

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
	_bAnimIsRunning(false),
	tictoc(0),
	currDir("../../../../Graceful_VidPro_Media/images"),
	_spinIsReady(true),
    ui(new Ui::vidStreamer)
{
    ui->setupUi(this);

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
	ui->pbAnim->setEnabled(false);
	ui->cbForceVGA->setEnabled(false);
	ui->sbSimultDelFactor->setEnabled(true);
	_iSimultDelFactor = ui->sbSimultDelFactor->value();


    refresh = new QTimer(this);
	avgfpsT = new QTimer(this);
	spinn = new cSpiNNcomm(this);

	// visualizer
#if(RENDERING_ENGINE==USE_QGRAPHICSVIEW)	// it has scrollbar functionality
	edge = new cScreen("Result Frame");
	orgImg = new cScreen("Original Frame");
#elif(RENDERING_ENGINE==USE_QGLWIDGET)
	edge = new cImgViewer("Result Frame");
	orgImg = new cImgViewer("Original Frame");
#else
	edge = new cPixViewer("Result Frame");
	orgImg = new cPixViewer("Original Frame");
#endif

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
	connect(ui->exFPS, SIGNAL(valueChanged(int)), this, SLOT(exFPSChanged(int)));

	connect(ui->cbFreq, SIGNAL(currentIndexChanged(int)), this, SLOT(changeGovernor(int)));

	connect(ui->sbNchips, SIGNAL(editingFinished()), this, SLOT(newChipsNum()));
	connect(ui->pbTest, SIGNAL(pressed()), this, SLOT(pbTestClicked()));

	connect(ui->cbSimultStreaming, SIGNAL(clicked(bool)), this, SLOT(updateSpinConnection()));
	connect(ui->sbSimultDelFactor, SIGNAL(valueChanged(int)), this, SLOT(updateSimultDelFactor(int)));

	updateSpinConnection();

	connect(refresh, SIGNAL(timeout()), this, SLOT(refreshUpdate()));

	// we need edgeRenderingDone for calculating fps
	connect(edge, SIGNAL(renderDone()), this, SLOT(edgeRenderingDone()));
	connect(orgImg, SIGNAL(renderDone()), this, SLOT(screenRenderingDone()));

	// expected FPS value
	_exFPS = ui->exFPS->value();

	refresh->setInterval(1000/_exFPS);

	//refresh->setInterval(100);   // which produces roughly 10fps
	//refresh->setInterval(1000);   // which produces roughly 1fps
	//refresh->setInterval(50);   // which produces roughly 20fps
	//refresh->setInterval(40);   // which produces roughly 25fps
	//refresh->setInterval(20);   // which produces roughly 50fps

	avgfpsT->setInterval(1000);		// it should tick every 1s
	connect(avgfpsT, SIGNAL(timeout()), this, SLOT(avgfpsT_tick()));
	avgfpsT->start();

	// additional setup
	cbSpiNNchanged(ui->cbSpiNN->currentIndex());
	connect(ui->cbSpiNN, SIGNAL(currentIndexChanged(int)), this, SLOT(cbSpiNNchanged(int)));

	oldNchips = 0;	// just for tracking if a new number of nodes is entered

	// let's test

	//ui->pbConfigure->click();
	//pbTestClicked();

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
#endif
}

vidStreamer::~vidStreamer()
{
	delete spinn;
	delete edge;
	delete orgImg;
	delete ui;
}

void vidStreamer::updateSpinConnection()
{
	connect(spinn, SIGNAL(frameOut(const QImage &)), edge, SLOT(putFrame(const QImage &)));
	connect(spinn, SIGNAL(sendFrameDone()), this, SLOT(frameSent()));

	if(ui->cbSimultStreaming->isChecked()) {
		ui->sbSimultDelFactor->setEnabled(true);
		// when we receive recvResultIsStarted from cspinncomm, let's stream frame to
		// spinnaker immediately, not waiting for edgeRenderingDone
		connect(spinn, SIGNAL(recvResultIsStarted()), this, SLOT(spinnSendFrame()));
	} else {
		ui->sbSimultDelFactor->setEnabled(false);
		// otherwise, wait until spinn finish transfering the frame
		connect(spinn, SIGNAL(frameOut(const QImage &)), this, SLOT(spinnSendFrame()));
	}


}

void vidStreamer::cbSpiNNchanged(int idx)
{
	if(idx==0) {
		ui->sbNchips->setValue(4);
		ui->sbNchips->setEnabled(false);
		ui->ipAddr->setText("192.168.240.253");
#if(DEBUG_LEVEL>1)
		ui->delFactorHost->setValue(10000);
		ui->delFactorSpin->setValue(250);
#else
		ui->delFactorHost->setValue(450);
		ui->delFactorSpin->setValue(15);
#endif
	} else {
		ui->sbNchips->setEnabled(true);
		ui->ipAddr->setText("192.168.240.1");
#if(DEBUG_LEVEL>1)
		ui->delFactorHost->setValue(10000);
		ui->delFactorSpin->setValue(250);
#else
		ui->delFactorHost->setValue(550);
		ui->delFactorSpin->setValue(25);
#endif
	}

}

void vidStreamer::pbConfigureClicked()
{
	orgImg->hide();
    edge->hide();
    ui->pbVideo->setEnabled(true);
    ui->pbImage->setEnabled(true);
    ui->pbSendImage->setEnabled(false);
	ui->pbPause->setEnabled(true);
    ui->cbTest->setEnabled(true);
    ui->pbTest->setEnabled(true);
	ui->pbAnim->setEnabled(true);
	ui->cbForceVGA->setEnabled(true);

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

	// if the governor is UserSpace, than the frequency must be supplied
	// otherwise, it uses the index of cbFreq
	quint8 freq;
	if(ui->cbFreq->currentIndex()==0)
		freq = ui->sbFreq->value();
	else
		freq = ui->cbFreq->currentIndex();

	quint8 nCorePreProc = ui->sbPixelWorkers->value();

	int delHost = ui->delFactorHost->text().toInt();
	int delSpin = ui->delFactorSpin->text().toInt();


	spinn->configSpin(spinIdx, nNodes, opType, wFilter, wHist, freq, nCorePreProc, delHost, delSpin);

	// just for the experiment for writing paper: coz it's boring...
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

void vidStreamer::errorString(QString err)
{
    qDebug() << err;
}

void vidStreamer::pbVideoClicked()
{
	currDir = "../../../../Graceful_VidPro_Media/videos";

	QString fName = QFileDialog::getOpenFileName(this, "Open Video File", currDir, "*");
    if(fName.isEmpty())
        return;
    ui->pbPause->setEnabled(true);

	worker = new QThread(this);
	decoder = new cDecoder(this);

	decoder->moveToThread(worker);
	connect(worker, SIGNAL(started()), decoder, SLOT(started()));
	connect(worker, SIGNAL(finished()), this, SLOT(cleanUpVideo()));
	//connect(decoder, SIGNAL(finished()), worker, SLOT(quit()));
	//connect(decoder, SIGNAL(finished()), decoder, SLOT(deleteLater()));
	//connect(worker, SIGNAL(finished()), worker, SLOT(deleteLater()));
	//connect(worker, SIGNAL(finished()), decoder, SLOT(deleteLater()));

	connect(decoder, SIGNAL(newFrame(const QImage &)), orgImg, SLOT(putFrame(const QImage &)));
#ifndef BYPASS_SPIN
	connect(decoder, SIGNAL(newFrame(const QImage &)), spinn, SLOT(frameIn(const QImage &)));
#endif
	connect(decoder, SIGNAL(newFrame(const QImage &)), this, SLOT(frameReady()));

	// for debugging only: send the image from decoder to edge-screener
	// connect(decoder, SIGNAL(newFrame(QImage)), edge, SLOT(putFrame(QImage)));

	connect(decoder, SIGNAL(gotPicSz(int,int)), this, SLOT(setSize(int,int)));
	connect(decoder, SIGNAL(finished()), this, SLOT(videoFinish()));
	connect(decoder, SIGNAL(error(QString)), this, SLOT(errorString(QString)));

	orgImg->show();
#ifndef BYPASS_SPIN
	edge->show();
#endif

	decoder->filename = fName;
	decoderIsActive = true;
	nRecvFrame = 0;
	ui->pbVideo->setEnabled(false);
	ui->pbAnim->setEnabled(false);

	refresh->start();

	worker->start();
}

void vidStreamer::refreshUpdate()
{
	_bRefresherUpdated = true;
	// vidstreamer manage the refreshing to create "centralized" update

	// if edge-widget finish rendering, then send the refresh signal
	// to the video decoder

	// debugging: bypassing edgeRenderingInProgress:
	// edgeRenderingInProgress = false;

	timespec temp;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &temp);

#if(DESTINATION==DEST_HOST)

#ifdef BYPASS_SPIN
	_spinIsReady = true;
	edgeRenderingInProgress = false;
#endif

	bool condition;
	if(ui->cbSimultStreaming->isChecked()) {
		condition = _spinIsReady && decoderIsActive;
		//qDebug() << "Using _spinIsReady";
	}
	else {
		condition = !edgeRenderingInProgress && decoderIsActive;
		//qDebug() << "not Using _spinIsReady";
	}

	if(condition) {
#ifndef BYPASS_SPIN
		if(ui->cbSimultStreaming->isChecked()) {
			giveDelay(simultDelVal);
		}
#endif
		get_fps();
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
	orgImg->setSize(w, h);
	edge->setSize(w,h);
    // then tell spinnaker to start initialization
#ifndef BYPASS_SPIN
    spinn->frameInfo(w, h);
#endif

	// calculate delay value for simultaneous buffering

	simultDelVal = ui->delFactorHost->value()*3;
	if(h>240) simultDelVal *= _iSimultDelFactor*100;

}

void vidStreamer::closeEvent(QCloseEvent *event)
{
	refresh->stop();
    if(worker != NULL && worker->isRunning())
        worker->quit();
	//screen->close();
	orgImg->close();
	edge->close();
	event->accept();
}

void vidStreamer::videoFinish()
{
	qDebug() << "Video ends";

	// disconnect signals
	disconnect(decoder, SIGNAL(newFrame(const QImage &)), orgImg, SLOT(putFrame(const QImage &)));
#ifndef BYPASS_SPIN
	disconnect(decoder, SIGNAL(newFrame(const QImage &)), spinn, SLOT(frameIn(const QImage &)));
#endif
	disconnect(decoder, SIGNAL(newFrame(const QImage &)), this, SLOT(frameReady()));
	disconnect(decoder, SIGNAL(gotPicSz(int,int)), this, SLOT(setSize(int,int)));
	disconnect(decoder, SIGNAL(finished()), this, SLOT(videoFinish()));
	disconnect(decoder, SIGNAL(error(QString)), this, SLOT(errorString(QString)));

	if(worker != NULL && worker->isRunning())
		worker->exit(0);
	decoderIsActive = false;
	refresh->stop();
	orgImg->hide();
#ifndef BYPASS_SPIN
	edge->hide();
#endif
}

void vidStreamer::cleanUpVideo()
{
	qDebug() << "Clean-up video memory";
	if(!worker->isRunning()) {
		delete decoder;
		delete worker;
	}
	ui->pbVideo->setEnabled(true);
	ui->pbAnim->setEnabled(true);
}

void vidStreamer::pbImageClicked()
{
	/* Note: this is how we measure high resolution time...
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
	*/

    /* Test-1: Send QImage-frame to SpiNNaker and display the result */
    if(experiment == 0) {
        imgFilename = QFileDialog::getOpenFileName(this, "Open Image File", currDir, "*");
        if(imgFilename.isEmpty())
            return;
    }
    currDir = QFileInfo(imgFilename).path(); // store path for next time

	if(experiment == 0)
		//screen->show();
		orgImg->show();

    ui->pbSendImage->setEnabled(true);

    loadedImage.load(imgFilename);

	setSize(loadedImage.width(), loadedImage.height());

	// for the "screen" widget, display the image immediately:
	orgImg->putFrame(loadedImage);
	orgImg->show();
}

void vidStreamer::pbSendImageClicked()
{
	/*
    if(experiment == 0)
        edge->show();
	*/

	// disable the button to see if the image is sent
    ui->pbSendImage->setEnabled(false);
	edge->show();

    // send the frame to spinn
	frameReady();
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tic);
	spinn->frameIn(loadedImage);
}

// frameReady() is called when the decoder has a new frame ready to be sent to spinn
void vidStreamer::frameReady()
{
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tic);

	// first, indicate that the "edge" widget is going to do the rendering
	// so the the refresh signal from the timer will be ignored in refreshUpdate()
	edgeRenderingInProgress = true;

	// for DEBUGGING: the following directly-to-edge-widget works:
	// edge->putFrame(frameIn);

#if(DEBUG_LEVEL>1)
	qDebug() << "Got new frame from decoder...";
#endif

	// turn off notification that spin is ready
	_spinIsReady = false;
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
#if(DEBUG_LEVEL>1)
	qDebug() << "Frame is sent to spinn...";
#endif
}

void vidStreamer::edgeRenderingDone()
{
// the counterpart of this can be seen on screenRenderingDone():
#ifdef USE_SPIN_OUTPUT_FOR_FPS
	nRecvFrame++;
#endif

	_bEdgeRenderingDone = true;
	//clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &toc);


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
	// set spinIsReady to true so that decoder can send frame immediately
	// while we receiving the result from spinnaker
	_spinIsReady = true;
	// qDebug() << "SpiNN is ready!";

#if(DEBUG_LEVEL>1)
	qDebug() << "SpiNNVid send the frame";
#endif
}

void vidStreamer::runAnimation()
{
	// let's try with qimage
	int wImg, hImg;
	if(ui->cbForceVGA->isChecked()) {
		wImg = 640;
		hImg = 480;
	} else {
		wImg = 320;
		hImg = 240;
	}
	int i, szFont = (wImg+hImg)/3;
	bool condition;

	setSize(wImg, hImg);

	QImage img(wImg,hImg,QImage::Format_RGB888);
	QPainter p(&img);
	p.setFont(QFont("Times", szFont, QFont::Bold));

	//p.fillRect(img.rect(), Qt::white);

	ui->pbVideo->setEnabled(false);

	// prepare avgfps counter
	nRecvFrame = 0;

	refresh->start();

	orgImg->show();
#ifndef BYPASS_SPIN
	edge->show();
#endif

	for(i=0; i<1000; i++) {

		if(!_bAnimIsRunning) break;

		p.drawText(img.rect(), Qt::AlignCenter | Qt::AlignVCenter, QString("%1").arg(i));
		orgImg->putFrame(img);
		_bEdgeRenderingDone = false;
		_bRefresherUpdated = false;
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tic);
#ifndef BYPASS_SPIN
		spinn->frameIn(img);
#endif

		while (1) {
			qApp->processEvents();

#ifdef BYPASS_SPIN
			_spinIsReady = true;
			_bEdgeRenderingDone = true;
#endif

			if(ui->cbSimultStreaming->isChecked())
				condition = _spinIsReady && _bRefresherUpdated;
			else
				condition = _bEdgeRenderingDone && _bRefresherUpdated;

			/*
			if(ui->cbSimultStreaming->isChecked())
				condition = _spinIsReady;
			else
				condition = _bEdgeRenderingDone;
			*/
			if(condition) break;

		}
#ifndef BYPASS_SPIN
		if(ui->cbSimultStreaming->isChecked()) {
			giveDelay(simultDelVal);
		}
#endif
		get_fps();
		p.eraseRect(img.rect());
		//p.fillRect(img.rect(), Qt::white);
	}

	refresh->stop();
	ui->pbVideo->setEnabled(true);
	return;

	/* The following is the old version...
	QString dir = QFileDialog::getExistingDirectory(this,
													"Open Anim Folder", currDir,
													QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
	if(dir.isNull() || dir.isEmpty()) return;
	currDir = dir;
	QString fName;
	int i=1;

	//double fps;

	fName = QString("%1/%2.jpg").arg(dir).arg(i);
	loadedImage.load(fName);
	setSize(loadedImage.width(), loadedImage.height());
	screen->putFrame(loadedImage);

	for(i=2; i<=99; i++) {
		//edge->clear();
		_bEdgeRenderingDone = false;
		_bRefresherUpdated = false;
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tic);
		//edge->putFrame(loadedImage);
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tic);
		spinn->frameIn(loadedImage);
		while (1) {
			qApp->processEvents();
			if(_bEdgeRenderingDone && _bRefresherUpdated) break;
			//if(_bEdgeRenderingDone) break;
		}
		//clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &toc);
		get_fps();
		fName = QString("%1/%2.jpg").arg(dir).arg(i);
		loadedImage.load(fName);
		screen->putFrame(loadedImage);
	}
	spinn->frameIn(loadedImage);
	*/
}

void vidStreamer::pbAnimClicked()
{

	if(!_bAnimIsRunning) {
		ui->pbAnim->setText("Stop");
		_bAnimIsRunning = true;
		runAnimation();
	}
	else {
		ui->pbAnim->setText("Animation");
		_bAnimIsRunning = false;
	}

}

void vidStreamer::exFPSChanged(int val)
{
	_exFPS = val;
	refresh->stop();
	refresh->setInterval(1000/val);
	refresh->start();
}

quint64 vidStreamer::getElapse_ns()
{
	timespec temp;
	quint64 df_ns;
	if ((toc.tv_nsec-tic.tv_nsec)<0) {
		temp.tv_sec = toc.tv_sec-tic.tv_sec-1;
		temp.tv_nsec = 1000000000+toc.tv_nsec-tic.tv_nsec;
	} else {
		temp.tv_sec = toc.tv_sec-tic.tv_sec;
		temp.tv_nsec = toc.tv_nsec-tic.tv_nsec;
	}
	// difference in nanosecond
	df_ns = temp.tv_sec*1000000000+temp.tv_nsec;
	return df_ns;
}

double vidStreamer::get_fps()
{
	return 0;

	// bypass...

	double fps;
	// note: tic must be collected when sending frame to spinnaker
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &toc);
	fps = 1000000000.0/(double)getElapse_ns();
	qDebug() << QString("fps = %1").arg(fps);
	return fps;
}

void vidStreamer::changeGovernor(int idx)
{
	if(idx==0)
		ui->sbFreq->setEnabled(true);
	else
		ui->sbFreq->setEnabled(false);
}

void vidStreamer::avgfpsT_tick()
{
//	if(decoderIsActive)
		ui->avgFPS->setText(QString("%1").arg(nRecvFrame));
//	else
//		ui->avgFPS->setText(QString("%1").arg(nRecvFrame/2));
	nRecvFrame = 0;
}

void vidStreamer::screenRenderingDone()
{
#ifndef USE_SPIN_OUTPUT_FOR_FPS
	nRecvFrame++;
	//qDebug() << "Done rendering org";
#endif
}


quint64 vidStreamer::elapsed(timespec start, timespec end)
{
	quint64 result;
	timespec temp;
	if ((end.tv_nsec-start.tv_nsec)<0) {
		temp.tv_sec = end.tv_sec-start.tv_sec-1;
		temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec-start.tv_sec;
		temp.tv_nsec = end.tv_nsec-start.tv_nsec;
	}
	result = temp.tv_sec*1000000000+temp.tv_nsec;
	return result;
}

void vidStreamer::giveDelay(quint32 ns)
{
	timespec ts, te;
	volatile quint32 dif;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
	do {
		qApp->processEvents();
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &te);
		dif = elapsed(ts, te);
	} while(dif < ns);
}
