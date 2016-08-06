#include "vidstreamer.h"
#include "ui_vidstreamer.h"

#include <QDebug>
#include <QMessageBox>
#include <QFileDialog>
#include <time.h>

vidStreamer::vidStreamer(QWidget *parent) :
    QWidget(parent),
    worker(NULL),
    decoder(NULL),
    isPaused(false),
    ui(new Ui::vidStreamer)
{
    ui->setupUi(this);
    //ui->cbSpiNN->setCurrentIndex(1);

	// disable buttons until spinnaker is configured
	ui->pbVideo->setEnabled(false);
	ui->pbImage->setEnabled(false);
	ui->pbSendImage->setEnabled(false);
	ui->pbPause->setEnabled(false);
	ui->cbTest->setEnabled(false);
	ui->pbTest->setEnabled(false);


    refresh = new QTimer(this);
    screen = new cScreen();     // this is for displaying original frame
    edge = new cScreen();       // this is for displaying result
    spinn = new cSpiNNcomm();

	connect(ui->pbVideo, SIGNAL(pressed()), this, SLOT(pbVideoClicked()));
	connect(ui->pbPause, SIGNAL(pressed()), this, SLOT(pbPauseClicked()));
	connect(ui->pbImage, SIGNAL(pressed()), this, SLOT(pbImageClicked()));
	connect(ui->pbSendImage, SIGNAL(pressed()), this, SLOT(pbSendImageClicked()));
	connect(ui->pbConfigure, SIGNAL(pressed()), this, SLOT(pbConfigureClicked()));
	connect(ui->sbNchips, SIGNAL(editingFinished()), this, SLOT(newChipsNum()));
	connect(ui->pbTest, SIGNAL(pressed()), this, SLOT(pbTestClicked()));
	connect(spinn, SIGNAL(frameOut(const QImage &)), edge, SLOT(putFrame(const QImage &)));
	connect(spinn, SIGNAL(frameOut(const QImage &)), this, SLOT(frameReady()));
	connect(spinn, SIGNAL(sendFrameDone()), this, SLOT(frameSent()));

	refresh->setInterval(40);   // which produces roughly 25fps
	//refresh->setInterval(1000);   // which produces roughly 1fps
	refresh->start();
	//connect(refresh, SIGNAL(timeout()), this, SLOT(refreshUpdate()));

	// additional setup
	cbSpiNNchanged(ui->cbSpiNN->currentIndex());
	connect(ui->cbSpiNN, SIGNAL(currentIndexChanged(int)), this, SLOT(cbSpiNNchanged(int)));

	// let's test

	//ui->pbConfigure->click();
	//pbTestClicked();
	/*
	spinn->setHost(1);	//1=spin5
	spinn->configSpin(1, 350, 350);
	*/

	oldNchips = 0;
	experiment = 5;

	if(experiment > 0) {
		//ui->rbLaplace->setChecked(true);
		ui->cbFreq->setCurrentIndex(1);
		switch(experiment){
		case 1: imgFilename = "../../../images/Elephant-vga.bmp"; break;
		case 2: imgFilename = "../../../images/Elephant-svga.bmp"; break;
		case 3: imgFilename = "../../../images/Elephant-xga.bmp"; break;
		case 4: imgFilename = "../../../images/Elephant-sxga.bmp"; break;
		case 5: imgFilename = "../../../images/Elephant-uxga.bmp"; break;
		}

	}
}

void vidStreamer::cbSpiNNchanged(int idx)
{
	if(idx==0) {
		ui->sbNchips->setValue(4);
		ui->sbNchips->setEnabled(false);
	} else {
		ui->sbNchips->setEnabled(true);
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
    if(ui->rbLaplace->isChecked()) opType = 1;
    quint8 wFilter = 0;
    if(ui->rbFilterOn->isChecked()) wFilter = 1;
    quint8 wHist = 0;
    if(ui->rbSharpOn->isChecked()) wHist = 1;
    quint8 freq = 200;
    if(ui->cbFreq->currentIndex()==1)
        freq = 250;
    spinn->configSpin(spinIdx, nNodes, opType, wFilter, wHist, freq);

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

	// use the following to send the image from decoder to edge-screener
	//connect(decoder, SIGNAL(newFrame(QImage)), edge, SLOT(putFrame(QImage)));

	connect(spinn, SIGNAL(frameOut(const QImage &)), edge, SLOT(putFrame(const QImage &)));
	connect(decoder, SIGNAL(gotPicSz(int,int)), this, SLOT(setSize(int,int)));
	connect(decoder, SIGNAL(finished()), this, SLOT(videoFinish()));
	//connect(refresh, SIGNAL(timeout()), decoder, SLOT(refresh()));
	connect(edge, SIGNAL(renderDone()), decoder, SLOT(refresh()));

	decoder->filename = fName;
	worker->start();
	//refresh->start();
}

void vidStreamer::refreshUpdate()
{
	return;	// disable 25fps update
	screen->drawFrame();
	edge->drawFrame();
}

void vidStreamer::setSize(int w, int h)
{
	screen->setSize(w,h);
    edge->setGeometry(edge->x()+w+20,edge->y(),w,h);
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
	refresh->stop();
	screen->hide();
	edge->hide();
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


    ui->pbSendImage->setEnabled(true);

    loadedImage.load(imgFilename);

    screen->putFrame(loadedImage);
    if(experiment == 0)
        screen->show();
    //send frame info to spinn
    spinn->frameInfo(loadedImage.width(), loadedImage.height());
}

void vidStreamer::pbSendImageClicked()
{
    if(experiment == 0)
        edge->show();
    // disable the button to see if the image is sent
    ui->pbSendImage->setEnabled(false);
    // send the frame to spinn
    spinn->frameIn(loadedImage);
}

void vidStreamer::frameReady()
{
    quint16 elapse = spinn->getSpinElapse();
    qDebug() << QString("SpiNNaker processing time = %1-clk").arg(elapse);
}

void vidStreamer::pbTestClicked()
{
    // Current possible test:
    // 0 - Workers ID
    // 1 - Network Configuration
    // 2 - BlockInfo
    // 3 - Workload
    // 4 - FrameInfo
    // 5 - Performance Measurement
    // 6 - PLL report
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
}
