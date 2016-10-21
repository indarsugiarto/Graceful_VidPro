#ifndef CSPINNCOMM_H
#define CSPINNCOMM_H

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QImage>

// to make it consistent with the aplx-part:
#include "defSpiNNVid.h"

#define SPIN3					0	// just a combobox index
#define SPIN5					1
#define SPIN3_IP				"192.168.240.253"
#define SPIN5_IP				"192.168.240.1"
#define ETH_PORT				255
#define ETH_ADDR				0


#define BLOCK_REPORT_NO         0
#define BLOCK_REPORT_YES        1

#define MAX_CHIPS               48  // how many chips will be used?

// just for nice indicator: doesn't work!
#define QTSLOT                  (void)

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
    // quint8 nodeID; // not needed anymore, since we can infer from data length
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
    cSpiNNcomm(QObject *parent=0);
    ~cSpiNNcomm();
	QImage *frResult;
	void sendTest(int testID);

public slots:
	void readResult();
	void readDebug();
	void configSpin(quint8 SpinIdx, quint8 nodesNum,
					quint8 edgeOperator, quint8 withFiltering,
					quint8 withSharping, quint8 freq, quint8 nCorePreProc,
					int delHost, int delSpin);
	void frameInfo(int imgW, int imgH);
	void frameIn(const QImage &);
	void sendImgLine(sdp_hdr_t h, uchar *pixel, quint16 len);
	// getSpinElapse() is useful if we want to read spinnaker processing time

signals:
	void sendFrameDone();
	void gotResult(const QByteArray &data);
	void gotReply(const QByteArray &data);
	void gotDebug(const QByteArray &data);
	void frameOut(const QImage &frame);
	// we can try to detect if a new result from spin just arrive
	// when we receive this, we can "signal" out so that vidstreamer
	// can alter _bRefresherUpdated immediately and the decoder
	// start streaming while vidstreamer receiving frame from spin
	void recvResultIsStarted();


private:
	QUdpSocket *sdpResult;
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
    sdp_hdr_t get_hdr(QByteArray const &ba);

	int sdpDelayFactorHost;
	int sdpDelayFactorSpin;		// not needed by GUI, but used by the APLX

	quint32 wImg, hImg, szImg;
	quint8 N_nodes;					// how many nodes are used in the network?
	quint8 nCore4PxProc;	// number of cores in root-node that handle pixel transmission

	// let's prepare header for image sending:
	// rgb, end-of-frame, config
	sdp_hdr_t hdrr, hdrg, hdrb, hdre, hdrc;

	QByteArray pxBuff;		// for storing result image sent via sdp

	uchar *rArray, *gArray, *bArray;	// line container

	qint32 frameID;	// this to tell SpiNN, which frame is being sent

	bool _recvResultIsStarted;

	// helper functions & variables
	void giveDelay(quint32 ns);	// in nanoseconds
	quint64 elapsed(timespec start, timespec end);

	// for debugging
	int recvLineCntr;
	int recvLine;
};

/*---------- Helper -----------*/
void readSDP(QByteArray ba);	// this is how to display QByteArray in hex


#endif // CSPINNCOMM_H
