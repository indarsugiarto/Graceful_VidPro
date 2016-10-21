#include "cspinncomm.h"
#include <QHostAddress>
#include <QDebug>
#include <QApplication>
#include <QColor>
#include <time.h>

/*------------ for debugging ----------------*/
#include <QFileDialog>

typedef struct mem_info {
	quint8 x;
	quint8 y;
	quint32 r;
	quint32 g;
	quint32 b;
	quint32 a;
	quint32 z;
	bool ready;
} t_mem_info;

t_mem_info imgBufAddr[48];	// max 48 nodes
int imgBufAddrCntr;

/*-------------------------------------------*/

// TODO: the following X_CHIPS and Y_CHIPS should be configurable
// but now, let's make it convenient
/*
uchar cSpiNNcomm::X_CHIPS[48] = {0,1,0,1,2,3,4,2,3,4,5,0,1,2,3,4,5,6,0,1,2,3,4,5,
                                 6,7,1,2,3,4,5,6,7,2,3,4,5,6,7,3,4,5,6,7,4,5,6,7};
uchar cSpiNNcomm::Y_CHIPS[48] = {0,0,1,1,0,0,0,1,1,1,1,2,2,2,2,2,2,2,3,3,3,3,3,3,
                                 3,3,4,4,4,4,4,4,4,5,5,5,5,5,5,6,6,6,6,6,7,7,7,7};
*/
// with circular rings
uchar cSpiNNcomm::X_CHIPS[48] = {0,1,1,0,2,2,2,1,0,3,3,3,3,2,1,0,4,4,4,4,4,3,2,1,
                                 5,5,5,5,5,4,3,2,6,6,6,6,6,5,4,3,7,7,7,7,7,6,5,4};
uchar cSpiNNcomm::Y_CHIPS[48] = {0,0,1,1,0,1,2,2,2,0,1,2,3,3,3,3,0,1,2,3,4,4,4,4,
                                 1,2,3,4,5,5,5,5,2,3,4,5,6,6,6,6,3,4,5,6,7,7,7,7};

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*-------------------- Class Constructor and Destructor ---------------------*/
cSpiNNcomm::cSpiNNcomm(QObject *parent): QObject(parent),
	frResult(NULL),
	frameID(0),	// start from-0
	_recvResultIsStarted(false)
{
    // initialize nodes
    for(int i=0; i<MAX_CHIPS; i++) {
        nodes[i].chipX = X_CHIPS[i];
        nodes[i].chipY = Y_CHIPS[i];
    }
    // create receiver sockets
    sdpResult = new QUdpSocket(this);
    sdpDebug = new QUdpSocket(this);
    sdpResult->bind(SDP_UDP_RESULT_PORT);
    sdpDebug->bind(SDP_UDP_DEBUG_PORT);
    // and the sender
    sender = new QUdpSocket(this);

	// handle/prepare callbacks
	connect(sdpResult, SIGNAL(readyRead()), this, SLOT(readResult()));
	connect(sdpDebug, SIGNAL(readyRead()), this, SLOT(readDebug()));

	// TODO: ask spinnaker, who's the leadAp
	// NOTE: core-1 is for the profiler
	leadAp = LEAD_CORE;

	// prepare headers for transmission
	hdrr.flags = 0x07;
	hdrr.tag = 0;
	// now we use several cores to handle pixel grayscaling & histogram counting
	// hdrr.dest_port = (SDP_PORT_R_IMG_DATA << 5) + leadAp;
	hdrr.dest_addr = 0;
	hdrr.srce_port = ETH_PORT;
	hdrr.srce_addr = ETH_ADDR;	// NOTE: this will always 0

	hdrg = hdrr; // hdrg.dest_port = (SDP_PORT_G_IMG_DATA << 5) + leadAp;
	hdrb = hdrr; // hdrb.dest_port = (SDP_PORT_B_IMG_DATA << 5) + leadAp;
	hdre = hdrr; // hdre.dest_port = (SDP_PORT_FRAME_END  << 5) + leadAp;
	hdrc = hdrr; hdrc.dest_port = (SDP_PORT_CONFIG     << 5) + leadAp;
}

cSpiNNcomm::~cSpiNNcomm()
{
	if(frResult != NULL)
		delete frResult;
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*------------------ Basic SpiNNaker communication mechanism ----------------*/
void cSpiNNcomm::sendSDP(sdp_hdr_t h, QByteArray s, QByteArray d)
{
	QByteArray ba = QByteArray(2, '\0');    // pad first
	ba.append(hdr(h));

	if(s.size()>0) ba.append(s);

	if(d.size()>0) ba.append(d);

	sender->writeDatagram(ba, ha, DEF_SEND_PORT);
	//sender->writeDatagram(ba, QHostAddress::Any, DEF_SEND_PORT);	// not working with Any
}

// scp() will construct a bytearray from a cmd_hdr_t variable
QByteArray cSpiNNcomm::scp(cmd_hdr_t cmd)
{
	QByteArray ba;

	QDataStream stream(&ba, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_4_8);
	stream.setByteOrder(QDataStream::LittleEndian);

	stream << cmd.cmd_rc << cmd.seq << cmd.arg1 << cmd.arg2 << cmd.arg3;
	return ba;
}

// hdr() will construct a bytearray from a sdp_hdr_t variable
QByteArray cSpiNNcomm::hdr(sdp_hdr_t h)
{
    /* It doesn't work, because ::number creates more bytes :(
    QByteArray ba = QByteArray::number(h.flags);
    ba.append(QByteArray::number(h.tag));
    ba.append(QByteArray::number(h.dest_port));
    ba.append(QByteArray::number(h.srce_port));
    ba.append(QByteArray::number(h.dest_addr));
    ba.append(QByteArray::number(h.srce_addr));
    return ba;
    */

    QByteArray ba;

	QDataStream stream(&ba, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_4_8);
	stream.setByteOrder(QDataStream::LittleEndian);

	stream << h.flags << h.tag << h.dest_port << h.srce_port << h.dest_addr << h.srce_addr;

	return ba;
}

// extract sdp_hdr from a bytearray
sdp_hdr_t cSpiNNcomm::get_hdr(QByteArray const &ba)
{
	sdp_hdr_t h;
	QByteArray b;
	b.resize(8);
	for(int i=0; i<8; i++) b[i] = ba[i+2];
	QDataStream stream(&b, QIODevice::ReadOnly);
	stream.setVersion(QDataStream::Qt_4_8);
	stream.setByteOrder(QDataStream::LittleEndian);
	stream >> h.flags >> h.tag >> h.dest_port >> h.srce_port >> h.dest_addr >> h.srce_addr;
	return h;
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*----------------- Image Processing / Network related stuffs----------------*/
// configSpin() should be called when Configure button is clicked
// Input:
//	- freq: SpiNN frequency, 0 = adaptive
//	- nCorePreProc: number of cores used for pixel retrieval
void cSpiNNcomm::configSpin(quint8 SpinIdx, quint8 nodesNum,
                            quint8 edgeOperator, quint8 withFiltering,
							quint8 withSharping, quint8 freq, quint8 nCorePreProc,
							int delHost, int delSpin)
{
	/*
	// first, send notification to reset the network to spinnaker

    cmd_hdr_t c;
    c.cmd_rc = SDP_CMD_RESET_NETWORK;
    sendSDP(h, scp(c));

    // give a delay
    giveDelay(100000);
	*/

	// send the new configuraiton
	quint8 opType, wFilter, wHistEq;

	N_nodes = nodesNum;				// by default it uses 4 nodes (spin3)
	opType = edgeOperator;			// operator typ: SOBEL, LAPLACE
	wFilter = withFiltering;		// with Gaussian filtering?
	wHistEq = withSharping;			// with Histogram Equalization?
	nCore4PxProc = nCorePreProc;	// will be used in frameIn()
	sdpDelayFactorHost = delHost;
	sdpDelayFactorSpin = delSpin;

	// collect the parameters
	if(SpinIdx==SPIN3) {
		ha = QHostAddress(QString(SPIN3_IP));
		qDebug() << QString("Will use SpiN-3 with %1 nodes").arg(N_nodes);
	}
	else {
		ha = QHostAddress(QString(SPIN5_IP));
		qDebug() << QString("Will use SpiN-5 with %1 nodes").arg(N_nodes);
	}

	qDebug() << QString("opType = %1, wFilter = %2, wHistEq = %3, freq = %4, HostDel = %5, SpinDel = %6")
				.arg(opType).arg(wFilter).arg(wHistEq).arg(freq).arg(delHost).arg(delSpin);


    // send to spinnaker
	// Note: we cannot modify srce_addr
	hdrc.srce_port = (uchar)sdpDelayFactorSpin;
	cmd_hdr_t c;
	c.cmd_rc = SDP_CMD_CONFIG_NETWORK;
	c.seq = (freq << 8) | (opType << 4) | (wFilter << 2) | wHistEq;

    // node-0 should be in chip<0,0>
    // node-1 is contained in arg1, node-2 is in arg2, and node-3 is in arg3
    c.arg1 = (nodes[1].chipX << 8) | (nodes[1].chipY);
    c.arg2 = (nodes[2].chipX << 8) | (nodes[2].chipY);
    c.arg3 = (nodes[3].chipX << 8) | (nodes[3].chipY);

	// build the node list (minimum #nodes is 4)
    QByteArray nodeList;
    // here,  we start from 4
    if(N_nodes > 4) {
        for(ushort i=4; i<N_nodes; i++) {
            nodeList.append(nodes[i].chipX);
            nodeList.append(nodes[i].chipY);
        }
    }
	sendSDP(hdrc, scp(c), nodeList);
}

// frameInfo() should be called right after file decoding
// It will instruct SpiNNaker to calculate workloads.
void cSpiNNcomm::frameInfo(int imgW, int imgH)
{
	hImg = imgH;
	wImg = imgW;
	szImg = imgH * imgW;

	// prepare the result
	if(frResult != NULL)
		delete frResult;
	frResult = new QImage(wImg, hImg, QImage::Format_RGB888);
	// and its buffer for storing data sent via sdp
	pxBuff.clear();

    // send to spinnaker
    cmd_hdr_t c;
	c.cmd_rc = SDP_CMD_FRAME_INFO;
	c.arg1 = (wImg << 16) | hImg;
	sendSDP(hdrc, scp(c));
}

void cSpiNNcomm::readDebug()
{
	QByteArray ba;
	ba.resize(sdpDebug->pendingDatagramSize());
	sdpDebug->readDatagram(ba.data(), ba.size());
	// then process it before emit the signal

	// especially for workload:
	sdp_hdr_t h;
	cmd_hdr_t c;
	quint16 pad;
	QDataStream stream(&ba, QIODevice::ReadOnly);
	stream.setVersion(QDataStream::Qt_4_8);
	stream.setByteOrder(QDataStream::LittleEndian);
	stream >> pad >> h.flags >> h.tag >> h.dest_port >> h.srce_port
		   >> h.dest_addr >> h.srce_addr
		   >> c.cmd_rc >> c.seq >> c.arg1 >> c.arg2 >> c.arg3;
	if(c.cmd_rc==DEBUG_REPORT_WLOAD){
		quint16 mxBlk = c.arg1 >> 8;
		quint16 blkID = c.arg1 & 0xFF;
		quint16 numWorker = c.arg2 >> 8;
		quint16 wID = c.arg2 & 0xFF;
		quint16 sLine = c.arg3 >> 16;
		quint16 eLine = c.arg3 & 0xFFFF;
		qDebug() << QString("Chip<%5,%6>\tblkID-%1\twID-%2\tsLine-%3\teLine-%4")
					.arg(blkID).arg(wID).arg(sLine).arg(eLine)
					.arg(h.srce_addr >> 8).arg(h.srce_addr & 0xFF);
	}
	else if(c.cmd_rc==DEBUG_REPORT_IMGBUF_IN) {
		quint8 node = c.seq;
		quint8 x = h.srce_addr >> 8;
		quint8 y = h.srce_addr & 0xFF;
//		qDebug() << QString("sp %1 %2").arg(x).arg(y);
//		qDebug() << QString("sdump red.%1%2 %3 %4").arg(x).arg(y).arg(c.arg1, 0, 16).arg(szImg, 0, 16);
//		qDebug() << QString("sdump green.%1%2 %3 %4").arg(x).arg(y).arg(c.arg2, 0, 16).arg(szImg, 0, 16);
//		qDebug() << QString("sdump blue.%1%2 %3 %4").arg(x).arg(y).arg(c.arg3, 0, 16).arg(szImg, 0, 16);
		imgBufAddr[node].x = x;
		imgBufAddr[node].y = y;
		imgBufAddr[node].r = c.arg1;
		imgBufAddr[node].g = c.arg2;
		imgBufAddr[node].b = c.arg3;
	}
	else if(c.cmd_rc==DEBUG_REPORT_IMGBUF_OUT) {
		quint8 node = c.seq;
//		quint8 x = h.srce_addr >> 8;
//		quint8 y = h.srce_addr & 0xFF;
//		qDebug() << QString("sdump gray.%1%2 %3 %4").arg(x).arg(y).arg(c.arg1, 0, 16).arg(szImg, 0, 16);
//		qDebug() << QString("sdump res.%1%2 %3 %4").arg(x).arg(y).arg(c.arg2, 0, 16).arg(szImg, 0, 16);
//		//qDebug() << QString("sdump blue.%1%2 %3 %4").arg(x).arg(y).arg(c.arg3, 0, 16).arg(szImg, 0, 16);
		imgBufAddr[node].a = c.arg1;
		imgBufAddr[node].z = c.arg2;
		imgBufAddr[node].ready = TRUE;
		imgBufAddrCntr++;
		if(imgBufAddrCntr==N_nodes) {
			for(int i=0; i<N_nodes; i++) {
				qDebug() << QString("sp %1 %2").arg(imgBufAddr[i].x).arg(imgBufAddr[i].y);
				qDebug() << QString("sdump red.%1%2 %3 %4").arg(imgBufAddr[i].x).arg(imgBufAddr[i].y)
							.arg(imgBufAddr[i].r, 0, 16).arg(szImg, 0, 16);
				qDebug() << QString("sdump green.%1%2 %3 %4").arg(imgBufAddr[i].x).arg(imgBufAddr[i].y)
							.arg(imgBufAddr[i].g, 0, 16).arg(szImg, 0, 16);
				qDebug() << QString("sdump blue.%1%2 %3 %4").arg(imgBufAddr[i].x).arg(imgBufAddr[i].y)
							.arg(imgBufAddr[i].b, 0, 16).arg(szImg, 0, 16);
				qDebug() << QString("sdump gray.%1%2 %3 %4").arg(imgBufAddr[i].x).arg(imgBufAddr[i].y)
							.arg(imgBufAddr[i].a, 0, 16).arg(szImg, 0, 16);
				qDebug() << QString("sdump res.%1%2 %3 %4").arg(imgBufAddr[i].x).arg(imgBufAddr[i].y)
							.arg(imgBufAddr[i].z, 0, 16).arg(szImg, 0, 16);
				qDebug() << "sleep 1";
			}
			qDebug() << "sp root";
			imgBufAddrCntr = 0;
		}
	}
}


void cSpiNNcomm::readResult()
{
	// in the aplx: resultMsg.srce_addr = lines;

	/* How reading the result should work:
	 * Regarding srce_port for debugging:
	 * 	1. for sending pixel, use srce_port as chunk index
			hence, the maximum chunk = 253, which reflect the maximum 253*272=68816 pixels
			in one line (thus, we can expect the maximum image size is 68816*whatever).
		2. for notifying host, use srce_port 0xFE
		   we cannot use 0xFF, because 0xFF is special for ETH
	 * srce_addr contains the line number of the image (useful for debugging???)
	*/

	QByteArray ba;
	ba.resize(sdpResult->pendingDatagramSize());
	sdpResult->readDatagram(ba.data(), ba.size());
	// at this point frResult should be ready

	// first, read the header
	sdp_hdr_t h = get_hdr(ba);
	// spinnaker finish sending the result?
	if(h.srce_port == SDP_SRCE_NOTIFY_PORT) {

		// first, turn off _recvResultIsStarted
		_recvResultIsStarted = false;

#if (DEBUG_LEVEL > 0)
		qDebug() << "Got SDP_SRCE_NOTIFY_PORT...";
#endif
		// then assemble the image from pxBuff (copy to frResult)
		int cntr = 0;
		QRgb value;
		for(int y=0; y<hImg; y++)
			for(int x=0; x<wImg; x++) {
				value = qRgb(pxBuff[cntr], pxBuff[cntr], pxBuff[cntr]);
				frResult->setPixel(x, y, value);
				cntr++;
			}
		// then emit the result
		emit frameOut(*frResult);

		// and clear pixel buffers
		pxBuff.clear();
		// how fast?
		//qDebug() << QString("Receiving %1 lines in ...-ms").arg(recvLineCntr);

	} else {
		// early inform vidstreamer about new result from spin
		if(!_recvResultIsStarted) {
			emit recvResultIsStarted();
			// turn on _recvResultIsStarted
			_recvResultIsStarted = true;
		}

		// for debugging:

		// we don't send "line" information anymore!
		// int ln = (int)h.srce_addr;
		int cID = (int)h.srce_port;

		/*
		// counting, how many lines has been received so far
		if(ln != recvLine) {
			recvLine = ln;	// toggle the flag, indicating a new line has arrive
			recvLineCntr++;
		}
		*/

		// remove header
		ba.remove(0, 10);
		// since the result is sent in sequential order, then no overlap
		// but beware with packet loss though!!!
		pxBuff.append(ba);

		// I want to see its data:
		//qDebug() << QString("Recv: %1-%2:%3").arg(ln).arg(cID).arg(ba.count());
	}
}

void cSpiNNcomm::sendImgLine(sdp_hdr_t h, uchar *pixel, quint16 len)
{
    QByteArray sdp = QByteArray(2, '\0');    // pad first
    sdp.append(hdr(h));
    if(len > 0) {
        QByteArray data = QByteArray::fromRawData((const char*)pixel, len);
        sdp.append(data);
    }
    sender->writeDatagram(sdp, ha, DEF_SEND_PORT);
	/* percobaan berikut membuktikan kalau flush()tidak pengaruh...
	 * karena semua data sudah otomatis terkirim, jadi ndak ada yang di-flush
	bool hasil;
	hasil = sender->flush();
	qDebug() << hasil;
	*/
}

/* Now we have to revise the frameIn() such that it send image data in ordered fashion:
 * [red_chunk] [green_chunk] [blue_chunk] ...delay... [red_chunk] [green_chunk] [blue_chunk] ...delay... etc...
 *
 *
 * Note: Do you know why we cannot use scanline with uchar directly? See this:
 *       http://stackoverflow.com/questions/2095039/qt-qimage-pixel-manipulation
 */
void cSpiNNcomm::frameIn(const QImage &f)
{
	// at this point SpiNNaker should know the configuration and
	// the frame info has also been sent to spinnaker

	// just for debugging if cSpiNNcomm can read QImage sent by cDecoder:
	//qDebug() << QString("Frame: %1 x %2").arg(f.width()).arg(f.height());
	//return;

	quint32 remaining = szImg, sz;

    // Kutemukan kenapa ada baris-baris yang aneh, sepertinya cara qt ini
    // berbeda dengan cara py sebelumnya, dimana cara py semua array disiapkan
    // dulu semuanya, bukan per baris. Karena itu aku ganti disini:

    // prepare line containers
    rArray = (uchar *)malloc(szImg);
    gArray = (uchar *)malloc(szImg);
    bArray = (uchar *)malloc(szImg);

	// TODO: cara di atas bikin ?Array NULL, kenapa?
	// karena szImg sebelumnya dibuat uint16, seharusnya uint32 (atau uint64)

	// prepare the container's pointer
	uchar *rPtr = rArray;
	uchar *gPtr = gArray;
	uchar *bPtr = bArray;

	quint16 pxSeq = 0;		// pixel chunk counter, a chunk is up to 272 pixels
	quint8 tag, srce_port;
	quint32 pxCntr = 0;
	quint8 core;

	// convert 2D image data into 1D array
	// THINK: is there faster way like using the copy mechanism (on direct address) ???
	//			NOTE: we cannot simply use scanLine!!!
	for(int i=0; i<hImg; i++) {
		for(int j=0; j<wImg; j++) {
			rArray[pxCntr] = QColor(f.pixel(j,i)).red();
			gArray[pxCntr] = QColor(f.pixel(j,i)).green();
			bArray[pxCntr] = QColor(f.pixel(j,i)).blue();
			pxCntr++;
		}
	}
	// now ?Array hold all pixel values

	/* Cannot use scanLine!!!
	memcpy(rArray, frame.scanLine(i)+1, wImg);
	memcpy(gArray, frame.scanLine(i)+wImg+1, wImg);
	memcpy(bArray, frame.scanLine(i)+2*wImg+1, wImg);
	*/

	//int delVal = 600;	// just OK with single image
	//int delVal = 1000;	// have distorted image on video
	int delVal = sdpDelayFactorHost;	// can be configured from gui
	// qDebug() << QString("Using delay factor %1").arg(delVal);
	// we work only with color images!!! No gray video :)
	// NOTE: we cannot alter srce_addr, hence we encode the pxSeq into tag + srce_port
	//		 so: tag==pxSeq.high and srce_port==pxSeq.low
	core = 0;
	while(remaining > 0) {
		tag = pxSeq >> 8;
		srce_port = pxSeq & 0xFF;
		sz = remaining > 272 ? 272:remaining;

		// split to nCore4PxProc
		hdrr.dest_port = (SDP_PORT_R_IMG_DATA << 5) | (core + LEAD_CORE);
		hdrg.dest_port = (SDP_PORT_G_IMG_DATA << 5) | (core + LEAD_CORE);
		hdrb.dest_port = (SDP_PORT_B_IMG_DATA << 5) | (core + LEAD_CORE);
		core = (core+1) < nCore4PxProc ? (core+1) : 0;

		/* Let's introduce small delay after each channel... */
		// the following is without delay (and works OK for single frame)

		hdrr.tag = tag; hdrr.srce_port = srce_port; sendImgLine(hdrr, rPtr, sz);
		hdrg.tag = tag; hdrg.srce_port = srce_port; sendImgLine(hdrg, gPtr, sz);
		hdrb.tag = tag; hdrb.srce_port = srce_port; sendImgLine(hdrb, bPtr, sz);



		/*
		hdrr.tag = tag; hdrr.srce_port = srce_port; sendImgLine(hdrr, rPtr, sz); giveDelay(DEF_QT_WAIT_VAL*delVal);
		hdrg.tag = tag; hdrg.srce_port = srce_port; sendImgLine(hdrg, gPtr, sz); giveDelay(DEF_QT_WAIT_VAL*delVal);
		hdrb.tag = tag; hdrb.srce_port = srce_port; sendImgLine(hdrb, bPtr, sz); giveDelay(DEF_QT_WAIT_VAL*delVal);
		*/

		// then give sufficient delay
		//giveDelay(DEF_QT_WAIT_VAL*5000);	// ini biasanya yang aku pakai
		//giveDelay(DEF_QT_WAIT_VAL*500);	// perfect, no SWC error, but sometimes miss
		//giveDelay(DEF_QT_WAIT_VAL*600);	// perfect, no SWC error
		//giveDelay(DEF_QT_WAIT_VAL*1000);	// slower...
		//giveDelay(DEF_QT_WAIT_VAL*50000);	// extreme slow for debugging
		giveDelay(DEF_QT_WAIT_VAL*delVal);
		// then adjust array position
		rPtr += sz;
		gPtr += sz;
		bPtr += sz;
		pxSeq++;	// prepare the next chunk
		remaining -= sz;
	}

	// then we have to send empty msg via SDP_PORT_FRAME_END to start decoding
	// first check if frameID needs to be reset
	if(frameID >= 0x7FFFFFF0)
		frameID = 0;
	else
		frameID++;
	// let's send to several cores:
	cmd_hdr_t c;
	c.arg1 = frameID;

	for(uchar core=LEAD_CORE; core<LEAD_CORE+N_CORE_FOR_EOF; core++) {
		hdre.dest_port = (SDP_PORT_FRAME_END  << 5) + core;
		sendSDP(hdre, scp(c));
	}
	// sendImgLine(hdre, NULL, 0); // deprecated!!!
	giveDelay(DEF_QT_WAIT_VAL*delVal);

	// emit signal
	emit sendFrameDone();

	// release memory
	free(rArray);
	free(gArray);
	free(bArray);

	// debugging:
	recvLine = -1;		// to check if the current line in the SDP is the same as the previous line
	recvLineCntr = 0;
}

void cSpiNNcomm::sendTest(int testID)
{
    // Current possible test:
    // 0 - Workers ID
    // 1 - Network Configuration
    // 2 - BlockInfo
    // 3 - Workload
    // 4 - FrameInfo
    // 5 - Performance Measurement
    // 6 - PLL report

    cmd_hdr_t c;
    c.cmd_rc = SDP_CMD_GIVE_REPORT;
    switch(testID) {
    case 0: c.seq = DEBUG_REPORT_WID; break;
    case 1: c.seq = DEBUG_REPORT_NET_CONFIG; break;
    case 2: c.seq = DEBUG_REPORT_BLKINFO; break;
    case 3: c.seq = DEBUG_REPORT_WLOAD; break;
    case 4: c.seq = DEBUG_REPORT_FRAMEINFO; break;
    case 5: c.seq = DEBUG_REPORT_PERF; break;
    case 6: c.seq = DEBUG_REPORT_PLL_INFO; break;
	case 7: c.seq = DEBUG_REPORT_IMGBUFS;
		for(int i=0; i<48; i++) imgBufAddr[i].ready = FALSE;
		imgBufAddrCntr = 0;
		break;
	case 8: c.seq = DEBUG_REPORT_TASKLIST; break;
	case 9: c.seq = DEBUG_REPORT_CLEAR_MEM; break;
	case 10: c.seq = DEBUG_REPORT_EDGE_DONE; break;

    }
	sendSDP(hdrc, scp(c));
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*--------------------------- Helper functions ------------------------------*/
void cSpiNNcomm::giveDelay(quint32 ns)
{
	timespec ts, te;
	quint32 dif;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
	do {
		QApplication::processEvents();
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &te);
		dif = elapsed(ts, te);
	} while(dif < ns);
}

quint64 cSpiNNcomm::elapsed(timespec start, timespec end)
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


/*---------------------- Helper function ----------------------*/
// helper function to see SDP
// NOTE: readSDP must have big delay to be seen!!!
void readSDP(QByteArray ba)
{
	// coba tampilkan bytearray sebagai hex:
	QByteArray hex = ba.toHex();
	QString str = "SDP(hex): ";
	for(int i=0; i<hex.count(); i++) {
		str.append(QString("%1").arg(hex.at(i)));
		if(i%2!=0)
			str.append(" ");
	}
	qDebug() << str;
}
