#ifndef CSPINNCOMM_H
#define CSPINNCOMM_H

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QImage>

// to make it consistent with the aplx-part:
#include "defSpiNNVid.h"

#define SPIN3       0
#define SPIN5       1
#define SPIN3_IP    "192.168.240.253"
#define SPIN5_IP    "192.168.240.1"
#define ETH_PORT    255
#define ETH_ADDR    0


#define BLOCK_REPORT_NO         0
#define BLOCK_REPORT_YES        1

#define MAX_CHIPS               48  // how many chips will be used?
typedef struct sdp_hdr		// SDP header
{
  uchar flags;
  uchar tag;
  uchar dest_port;
  uchar srce_port;
  quint16 dest_addr;
  quint16 srce_addr;
} sdp_hdr_t;

typedef struct cmd_hdr		// Command header
{
  quint16 cmd_rc;
  quint16 seq;
  quint32 arg1;
  quint32 arg2;
  quint32 arg3;
} cmd_hdr_t;

typedef struct nodeInfo
{
    quint8 chipX;
    quint8 chipY;
    quint8 nodeID;
} nodes_t;


// struct timespec is already defined in time.h
/*
struct timespec {
	time_t tv_sec;	// seconds
	long tv_nsec;	// nanoseconds
};
*/

class cSpiNNcomm: public QObject
{
	Q_OBJECT

public:
    cSpiNNcomm(quint8 nodes=4, quint8 opType=0, quint8 wFilter=0, quint8 wHist=0, QObject *parent=0);
    ~cSpiNNcomm();
	QImage *frResult;

public slots:
	void readResult();
	void readReply();
	void readDebug();
    void configSpin(uchar spinIDX, int imgW, int imgH);
    void setHost(int spinIDX);  //0=spin3, 1=spin5
	void frameIn(const QImage &frame);
	void sendReply();
	void sendImgLine(sdp_hdr_t h, uchar *pixel, quint16 len);

signals:
	void gotResult(const QByteArray &data);
	void gotReply(const QByteArray &data);
	void gotDebug(const QByteArray &data);
	void frameOut(const QImage &frame);

private:
	QUdpSocket *sdpResult;
	QUdpSocket *sdpReply;
	QUdpSocket *sdpDebug;
    QUdpSocket *sender;
private:
    nodes_t     nodes[MAX_CHIPS];
    QHostAddress ha;
    uchar leadAp;
    static uchar X_CHIPS[48];       // must be static, otherwise it'll raise
    static uchar Y_CHIPS[48];       // is not a static data member of cSpiNNcomm
	void sendSDP(sdp_hdr_t h, QByteArray s = QByteArray(), QByteArray d = QByteArray());
    // helper functions:
    QByteArray hdr(sdp_hdr_t h);
    QByteArray scp(cmd_hdr_t cmd);
	volatile bool cont2Send;

	quint16 wImg, hImg, szImg;
	quint8 N_nodes;
	quint8 edgeOperator, withFilter, withHistogramEq;

	// let's prepare header for image sending
	sdp_hdr_t hdrr, hdrg, hdrb;

	volatile bool ResultTriggered;
	volatile bool w272;				// will be used to indicate that the width is special
	QByteArray pixelBuffer;
	QByteArray pxBuff[3];


	// helper functions & variables
	void giveDelay(quint32 ns);	// in nanoseconds
	quint64 elapsed(timespec start, timespec end);
};

#endif // CSPINNCOMM_H
