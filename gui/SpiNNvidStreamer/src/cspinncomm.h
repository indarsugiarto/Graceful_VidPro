#ifndef CSPINNCOMM_H
#define CSPINNCOMM_H

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QImage>

#define SPIN3       0
#define SPIN5       1
#define SPIN3_IP    "192.168.240.253"
#define SPIN5_IP    "192.168.240.1"
#define ETH_PORT    255
#define ETH_ADDR    0

#define SDP_CMD_CONFIG_CHAIN    11

#define OP_SOBEL_NO_FILTER      1
#define OP_SOBEL_WITH_FILTER    2
#define OP_LAPLACE_NO_FILTER    3
#define OP_LAPLACE_WITH_FILTER  4

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
    ushort chipX;
    ushort chipY;
    ushort nodeID;
} nodes_t;

#define SDP_REPLY_PORT  20000		// with tag 1
#define SDP_RESULT_PORT	20001		// with tag 2
#define SDP_DEBUG_PORT  20002		// with tag 3
#define DEF_SEND_PORT	17893		// tidak bisa diganti dengan yang lain
#define SDP_IMAGE_CHUNK	272
#define SDP_

class cSpiNNcomm: public QObject
{
	Q_OBJECT

public:
    cSpiNNcomm(QObject *parent=0);
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
    uchar sdpImgRedPort;            // = 1       # based on the aplx code
    uchar sdpImgGreenPort;          // = 2
    uchar sdpImgBluePort;           // = 3
	uchar sdpImgReplyPort;			// = 6
    uchar sdpImgConfigPort;         // = 7
    static uchar X_CHIPS[48];       // must be static, otherwise it'll raise
    static uchar Y_CHIPS[48];       // is not a static data member of cSpiNNcomm
	void sendSDP(sdp_hdr_t h, QByteArray s = QByteArray(), QByteArray d = QByteArray());
    // helper functions:
    QByteArray hdr(sdp_hdr_t h);
    QByteArray scp(cmd_hdr_t cmd);
	volatile bool cont2Send;

	quint16 wImg, hImg, szImg;

	// let's prepare header for image sending
	sdp_hdr_t hdrr, hdrg, hdrb;

	volatile bool ResultTriggered;
	volatile bool w272;				// will be used to indicate that the width is special
	QByteArray pixelBuffer;
	QByteArray pxBuff[3];
};

#endif // CSPINNCOMM_H
