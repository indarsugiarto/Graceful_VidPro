#include "vidstreamer.h"
#include "ui_vidstreamer.h"

#include <QDebug>
#include <QMessageBox>
#include <QFileDialog>
#include <time.h>
#include <QDesktopWidget>	// needed for QApplication::desktop()->screenGeometry();

vidStreamer::vidStreamer(QWidget *parent) :
    QWidget(parent),
    worker(NULL),
    decoder(NULL),
    isPaused(false),
	edgeRenderingInProgress(false),
	decoderIsActive(false),
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
	connect(ui->sbNchips, SIGNAL(editingFinished()), this, SLOT(newChipsNum()));
	connect(ui->pbTest, SIGNAL(pressed()), this, SLOT(pbTestClicked()));
	connect(ui->pbShowHideTest, SIGNAL(pressed()), this, SLOT(pbShowHideTestClicked()));
	connect(spinn, SIGNAL(frameOut(const QImage &)), edge, SLOT(putFrame(const QImage &)));
	connect(spinn, SIGNAL(frameOut(const QImage &)), this, SLOT(spinnSendFrame()));

	connect(spinn, SIGNAL(sendFrameDone()), this, SLOT(frameSent()));
	connect(edge, SIGNAL(renderDone()), this, SLOT(edgeRenderingDone()));
	connect(refresh, SIGNAL(timeout()), this, SLOT(refreshUpdate()));


	refresh->setInterval(40);   // which produces roughly 25fps
	//refresh->setInterval(1000);   // which produces roughly 1fps
	//refresh->setInterval(500);   // which produces roughly 2fps
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

	spinn->configSpin(spinIdx, nNodes, opType, wFilter, wHist, freq, nCorePreProc);

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
	//connect(decoder, SIGNAL(newFrame(const QImage &)), spinn, SLOT(frameIn(const QImage &)));
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
	//qDebug() << "Tick";

	// vidstreamer manage the refreshing to create "centralized" update

	// if edge-widget finish rendering, then send the refresh signal
	// to the video decoder

	// debugging: bypassing edgeRenderingInProgress:
	//edgeRenderingInProgress = false;
#if(DESTINATION==DEST_HOST)

	if(!edgeRenderingInProgress && decoderIsActive)
		decoder->refresh();
#else
	if(decoderIsActive)
		decoder->refresh();
#endif
}

void vidStreamer::setSize(int w, int h)
{
	screen->setSize(w,h);
	//edge->setGeometry(edge->x()+w+20,edge->y(),w,h);
	//edge->clear();
	//edge->hide();
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
	//refresh->stop();
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
        imgFilename = QFileDialog::getOpenFileName(this, "Open Image File", "../../../images", "*");
        if(imgFilename.isEmpty())
            return;
    }

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
    spinn->frameIn(loadedImage);
}

// frameReady() is called when the decoder has a new frame ready to be sent to spinn
void vidStreamer::frameReady(const QImage &frameIn)
{
	// first, indicate that the "edge" widget is going to do the rendering
	// so the the refresh signal from the timer will be ignored in refreshUpdate()
	edgeRenderingInProgress = true;

	// the following direct to edge widget works:
	// edge->putFrame(frameIn);

	//loadedImage = frameIn;
	spinn->frameIn(frameIn);

	//qDebug() << "Got new frame from decoder...";
}

void vidStreamer::pbTestClicked()
{
    spinn->sendTest(ui->cbTest->currentIndex());
}

void vidStreamer::newChipsNum()
{
    //qDebug() << QString("%1").arg(ui->sbNchips->value());
    if(ui->sbNchips->value() != oldNchips) {
        oldNchips = ui->sbNchips->value();
        pbConfigureClicked();
    }
}

void vidStreamer::frameSent()
{
    // if using "Load Image"
    if(!loadedImage.isNull())
        ui->pbSendImage->setEnabled(true);
	//qDebug() << "Frame is sent to spinn...";
}

void vidStreamer::edgeRenderingDone()
{
	// indicate that the "edge" widget is finish in rendering a frame
	edgeRenderingInProgress = false;
	//qDebug() << "Edge rendering done...";
}

void vidStreamer::spinnSendFrame()
{
	//qDebug() << "SpiNNVid send the frame";
}
