#include "cspinncomm.h"
#include <QHostAddress>
#include <QDebug>
#include <QApplication>
#include <QColor>
#include <time.h>

//for debugging
#include <QFileDialog>

// TODO: the following X_CHIPS and Y_CHIPS should be configurable
// but now, let's make it convenient
uchar cSpiNNcomm::X_CHIPS[48] = {0,1,0,1,2,3,4,2,3,4,5,0,1,2,3,4,5,6,0,1,2,3,4,5,
                                 6,7,1,2,3,4,5,6,7,2,3,4,5,6,7,3,4,5,6,7,4,5,6,7};
uchar cSpiNNcomm::Y_CHIPS[48] = {0,0,1,1,0,0,0,1,1,1,1,2,2,2,2,2,2,2,3,3,3,3,3,3,
                                 3,3,4,4,4,4,4,4,4,5,5,5,5,5,5,6,6,6,6,6,7,7,7,7};

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*-------------------- Class Constructor and Destructor ---------------------*/
cSpiNNcomm::cSpiNNcomm(QObject *parent): QObject(parent),
    frResult(NULL)
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
	// right now, let's assume it is core-1
	leadAp = 1;

	// prepare headers for transmission
	hdrr.flags = 0x07;
	hdrr.tag = 0;
	hdrr.dest_port = (SDP_PORT_R_IMG_DATA << 5) + leadAp;
	hdrr.dest_addr = 0;
	hdrr.srce_port = ETH_PORT;
	hdrr.srce_addr = ETH_ADDR;

	hdrg = hdrr; hdrg.dest_port = (SDP_PORT_G_IMG_DATA << 5) + leadAp;
	hdrb = hdrr; hdrb.dest_port = (SDP_PORT_B_IMG_DATA << 5) + leadAp;
	hdre = hdrr; hdre.dest_port = (SDP_PORT_FRAME_END  << 5) + leadAp;
	hdrf = hdrr; hdrf.dest_port = (SDP_PORT_FRAME_INFO << 5) + leadAp;
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
// it should be called when Configure button is clicked
void cSpiNNcomm::configSpin(quint8 SpinIdx, quint8 nodesNum, quint8 edgeOperator, quint8 withFiltering, quint8 withSharping)
{
    // collect the parameters
    if(SpinIdx==SPIN3) {
        ha = QHostAddress(QString(SPIN3_IP));
        qDebug() << "Will use SpiN-3";
    }
    else {
        ha = QHostAddress(QString(SPIN5_IP));
        qDebug() << "Will use SpiN-5";
    }

    N_nodes = nodesNum;			// by default it uses 4 nodes (spin3)
    opType = edgeOperator;      // operator typ: SOBEL, LAPLACE
    wFilter = withFiltering;	// with Gaussian filtering?
    wHistEq = withSharping;     // with Histogram Equalization?

    // send to spinnaker
    sdp_hdr_t h;
    h.flags = 0x07;
    h.tag = 0;
    h.dest_port = (SDP_PORT_CONFIG << 5) + leadAp;
    h.srce_port = ETH_PORT;
    h.dest_addr = 0;
    h.srce_addr = 0;

    cmd_hdr_t c;
    c.cmd_rc = SDP_CMD_CONFIG_NETWORK;
    c.seq = (opType << 8) | (wFilter << 4) | wHistEq;
    // node-0 should be in chip<0,0>
    // node-1 is contained in arg1, node-2 is in arg2, and node-3 is in arg3
    c.arg1 = (nodes[1].chipX << 8) | (nodes[1].chipY);
    c.arg2 = (nodes[2].chipX << 8) | (nodes[2].chipY);
    c.arg3 = (nodes[3].chipX << 8) | (nodes[3].chipY);

    // build the node list
    QByteArray nodeList;
    // here,  we start from 4
    if(N_nodes > 4) {
        for(ushort i=4; i<N_nodes; i++) {
            nodeList.append(nodes[i].chipX);
            nodeList.append(nodes[i].chipY);
        }
    }
    sendSDP(h, scp(c), nodeList);
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
	// and its buffer
	pxBuff.clear();

    // send to spinnaker
    cmd_hdr_t c;
    c.cmd_rc = wImg;
    c.seq = hImg;
    sendSDP(hdrf, scp(c));
}

void cSpiNNcomm::readDebug()
{
	QByteArray ba;
	ba.resize(sdpDebug->pendingDatagramSize());
	sdpDebug->readDatagram(ba.data(), ba.size());
	// then process it before emit the signal
}

void cSpiNNcomm::readResult()
{
	// in the aplx: resultMsg.srce_addr = lines;

	QByteArray ba;
	ba.resize(sdpResult->pendingDatagramSize());
	sdpResult->readDatagram(ba.data(), ba.size());
	// at this point frResult should be ready

	// first, read the header
	sdp_hdr_t h = get_hdr(ba);
	// spinnaker finish sending the result?
	if(h.srce_port != SDP_SRCE_NOTIFY_PORT) {

		// for debugging:
		int ln = (int)h.srce_addr;
		if(ln != recvLine) {
			recvLine = ln;
			recvLineCntr++;
		}

		// remove header
		ba.remove(0, 10);
		// since the result is sent in sequential order, then no overlap
		// but beware with packet loss though!!!
		pxBuff.append(ba);
	}
	// Note: we receive a result notification (only gray result!)
	else {
		// copy to frResult
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
		spinElapse = h.srce_addr;
		qDebug() << QString("Receiving %1 lines").arg(recvLineCntr);
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
}

/* Now we have to revise the frameIn() such that it send image data in ordered fashion:
 * [red_chunk] [green_chunk] [blue_chunk] ...delay... [red_chunk] [green_chunk] etc...
 *
 *
 * Note: Do you know why we cannot use scanline with uchar directly? See this:
 *       http://stackoverflow.com/questions/2095039/qt-qimage-pixel-manipulation
 */
void cSpiNNcomm::frameIn(const QImage &frame)
{
	// at this point SpiNNaker should know the configuration and
	// the frame info has also been sent to spinnaker

	quint32 remaining = szImg, sz;

    // Kutemukan kenapa ada baris-baris yang aneh, sepertinya cara qt ini
    // berbeda dengan cara py sebelumnya, dimana cara py semua array disiapkan
    // dulu semuanya, bukan per baris. Karena itu aku ganti disini:

    // prepare line containers
    rArray = (uchar *)malloc(szImg);
    gArray = (uchar *)malloc(szImg);
    bArray = (uchar *)malloc(szImg);

	TODO: cara di atas bikin xArray NULL, kenapa? Coba nanti buat aja QByteArray

	// prepare the container's pointer
	uchar *rPtr;
	uchar *gPtr;
	uchar *bPtr;

	quint16 pxSeq = 0;
	quint8 tag, srce_port;
	quint32 pxCntr = 0;

	// for all lines in the image
	for(int i=0; i<hImg; i++) {
		for(int j=0; j<wImg; j++) {
			rArray[pxCntr] = QColor(frame.pixel(j,i)).red();
			gArray[pxCntr] = QColor(frame.pixel(j,i)).green();
			bArray[pxCntr] = QColor(frame.pixel(j,i)).blue();
			pxCntr++;
		}
	}
	// now ?Array hold all pixel values

	/* Cannot use scanLine!!!
	memcpy(rArray, frame.scanLine(i)+1, wImg);
	memcpy(gArray, frame.scanLine(i)+wImg+1, wImg);
	memcpy(bArray, frame.scanLine(i)+2*wImg+1, wImg);
	*/

	// then reset the pointers to the beginning of the lines
	rPtr = rArray;
	gPtr = gArray;
	bPtr = bArray;

	// we work only with color images!!!
	while(remaining > 0) {
		tag = pxSeq >> 8;
		srce_port = pxSeq & 0xFF;
		sz = remaining > 272 ? 272:remaining;

		hdrr.tag = tag; hdrr.srce_port = srce_port;
		hdrg.tag = tag; hdrg.srce_port = srce_port;
		hdrb.tag = tag; hdrb.srce_port = srce_port;
		//hdrr.srce_addr = pxSeq;
		//hdrg.srce_addr = pxSeq;
		//hdrb.srce_addr = pxSeq;
		sendImgLine(hdrr, rPtr, sz);
		sendImgLine(hdrg, gPtr, sz);
		sendImgLine(hdrb, bPtr, sz);

		// then give sufficient delay
		giveDelay(DEF_QT_WAIT_VAL*1000000);
		// then adjust array position
		rPtr += sz;
		gPtr += sz;
		bPtr += sz;
		pxSeq++;
		remaining -= sz;
	}

	qDebug() << QString("Total pxSeq = %1").arg(pxSeq);
	// then we have to send empty msg via SDP_PORT_FRAME_END to start decoding
	// sendImgLine(hdre, NULL, 0);

	// release memory
	free(rArray);
	free(gArray);
	free(bArray);

	// debugging:
	recvLine = -1;
	recvLineCntr = 0;
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
