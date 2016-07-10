#include "vidstreamer.h"
#include "ui_vidstreamer.h"

#include <QDebug>
#include <QMessageBox>
#include <QFileDialog>

vidStreamer::vidStreamer(QWidget *parent) :
    QWidget(parent),
    worker(NULL),
    decoder(NULL),
    isPaused(false),
    ui(new Ui::vidStreamer)
{
    ui->setupUi(this);
	ui->cbSpiNN->setCurrentIndex(1);
    refresh = new QTimer(this);
    screen = new cScreen();
    edge = new cScreen();
    spinn = new cSpiNNcomm();
    spinn->setHost(ui->cbSpiNN->currentIndex());

    connect(ui->pbLoad, SIGNAL(pressed()), this, SLOT(pbLoadClicked()));
	connect(ui->pbPause, SIGNAL(pressed()), this, SLOT(pbPauseClicked()));
    connect(ui->cbSpiNN, SIGNAL(currentIndexChanged(int)), spinn,
            SLOT(setHost(int)));

	refresh->setInterval(40);   // which produces roughly 25fps
	//refresh->setInterval(1000);   // which produces roughly 1fps
	refresh->start();
	//connect(refresh, SIGNAL(timeout()), this, SLOT(refreshUpdate()));


	// let's test
	/*
	spinn->setHost(1);	//1=spin5
	spinn->configSpin(1, 350, 350);
	*/

    // test functionality:
    connect(ui->pbTest, SIGNAL(pressed()), this, SLOT(pbTestClicked()));
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

	// set correct spinnaker board
	spinn->setHost(ui->cbSpiNN->currentIndex());

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
    spinn->configSpin(ui->cbSpiNN->currentIndex(), w, h);
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
    /* Test-1: Send QImage-frame to SpiNNaker and display the result */
    QString fName = QFileDialog::getOpenFileName(this, "Open Image File", "../../../../images", "*");
    if(fName.isEmpty())
        return;

    // set correct spinnaker board
    spinn->setHost(ui->cbSpiNN->currentIndex());

    connect(spinn, SIGNAL(frameOut(const QImage &)), edge, SLOT(putFrame(const QImage &)));
    QImage img;
    img.load(fName);

    edge->setSize(img.width(), img.height());   // to show the viewer
    spinn->configSpin(ui->cbSpiNN->currentIndex(), img.width(), img.height());

    spinn->frameIn(img);
}

