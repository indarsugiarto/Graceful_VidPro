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
	ui->pbLoad->setEnabled(false);
	ui->pbTest->setEnabled(false);
	ui->pbPause->setEnabled(false);

    refresh = new QTimer(this);
    screen = new cScreen();     // this is for displaying original frame
    edge = new cScreen();       // this is for displaying result
    spinn = new cSpiNNcomm();

    connect(ui->pbLoad, SIGNAL(pressed()), this, SLOT(pbLoadClicked()));
	connect(ui->pbPause, SIGNAL(pressed()), this, SLOT(pbPauseClicked()));
	connect(ui->pbTest, SIGNAL(pressed()), this, SLOT(pbTestClicked()));
	connect(ui->pbConfigure, SIGNAL(pressed()), this, SLOT(pbConfigureClicked()));
	connect(spinn, SIGNAL(frameOut(const QImage &)), edge, SLOT(putFrame(const QImage &)));

	refresh->setInterval(40);   // which produces roughly 25fps
	//refresh->setInterval(1000);   // which produces roughly 1fps
	refresh->start();
	//connect(refresh, SIGNAL(timeout()), this, SLOT(refreshUpdate()));

	// additional setup
	cbSpiNNchanged(ui->cbSpiNN->currentIndex());
	connect(ui->cbSpiNN, SIGNAL(currentIndexChanged(int)), this, SLOT(cbSpiNNchanged(int)));

	// let's test
	/*
	spinn->setHost(1);	//1=spin5
	spinn->configSpin(1, 350, 350);
	*/

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
    ui->pbLoad->setEnabled(true);
    ui->pbTest->setEnabled(true);
    ui->pbPause->setEnabled(true);

    // then send information to spinn
    quint8 spinIdx = ui->cbSpiNN->currentIndex();
    quint8 nNodes = ui->sbNchips->value();
    quint8 opType = 0;
    if(ui->rbLaplace->isChecked()) opType = 1;
    quint8 wFilter = 0;
    if(ui->rbFilterOn->isChecked()) wFilter = 1;
    quint8 wHist = 0;
    if(ui->rbSharpOn->isChecked()) wHist = 1;
    spinn->configSpin(spinIdx, nNodes, opType, wFilter, wHist);
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

void vidStreamer::pbLoadClicked()
{
    QString fName = QFileDialog::getOpenFileName(this, "Open Video File", "../../../../videos", "*");
    if(fName.isEmpty())
        return;

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

void vidStreamer::pbTestClicked()
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
    QString fName = QFileDialog::getOpenFileName(this, "Open Image File", "../../../../images", "*");
    if(fName.isEmpty())
        return;


    QImage img;
    img.load(fName);

    screen->putFrame(img);
    screen->show();
    edge->show();
    connect(spinn, SIGNAL(frameOut(const QImage &)), this, SLOT(frameReady()));

    // send frame info to spinn
    spinn->frameInfo(img.width(), img.height());
    // send the frame to spinn
    spinn->frameIn(img);
}

void vidStreamer::frameReady()
{
    quint16 elapse = spinn->getSpinElapse();
    qDebug() << QString("SpiNNaker processing time = %1-clk").arg(elapse);
}

