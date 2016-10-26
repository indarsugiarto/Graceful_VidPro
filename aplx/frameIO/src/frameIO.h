/* For converting rgb to gray, we have to use stdfix.
 * Here, we have to define the weight for the conversion. For simplicity, we can
 * use matlab version:
 * 0.2989 * R + 0.5870 * G + 0.1140 * B
 * (http://uk.mathworks.com/help/matlab/ref/rgb2gray.html)
 * */
#ifndef SPINNVID_H
#define SPINNVID_H

#include <spin1_api.h>
#include <stdfix.h>
#include "../../SpiNNVid/src/defSpiNNVid.h"

// how many cores are allocated for each pipeline: sdpRecv, pxFwdr, and mcplRecv
uint nCorePerPipe;

/* In this version: implementing Histogram Equalization in frameIO
 *
 * */

#define MCPL_FRAMEIO_MASK			0xFFFF0000	// so it contains header and arg

#define MCPL_FRAMEIO_FWD_WID		0xF2A10000	// from LEAD_CORE to pxFwdr
#define MCPL_FRAMEIO_SZFRAME		0xF2A20000	// from LEAD_CORE to core 7-11 and 17
#define MCPL_FRAMEIO_NEWGRAY		0xF2A30000	// from sdpRecv to pxFwdr
#define MCPL_FRAMEIO_EOF_INT		0xF2A40000	// from LEAD_CORE to pxFwdr
#define MCPL_FRAMEIO_HIST_CNTR_NEXT	0xF2A50000	// within pxFwdr
#define MCPL_FRAMEIO_HIST_RDY		0xF2A60000	// from last core in pxFwdr to pxFwdr
#define MCPL_FRAMEIO_EOF_EXT_RDY	0xF2A70000	// within pxFwdr
#define MCPL_FRAMEIO_EOF_EXT		0xF2A80000	// from last core in pxFwdr to external

/*------------------------ Struct, Enum, Type definition ----------------------------*/


// Coba cara baru, multiple cores receive frames consecutively. This way, each core
// will have enough time to do grayscaling, histogram, storing/broadcasting.
// NOTE: pxSeq is the chunk seqment and is contained in the tag and srce_port in
//		 the SDP data sent to root-node.
typedef struct pxBuf {
	ushort pxSeq;	// the segment index of the chunk within the overall frame structure
					// with 0xFFFF signifies EOF (end of frame)
	ushort pxLen;	// how many pixels are contained in one channel chunk
	ushort pxCntr[4];	// for receiver only, to track which pixels are broadcasted

	uchar *rpxbuf;
	uchar *gpxbuf;
	uchar *bpxbuf;
	uchar *ypxbuf;

	uchar newFrameRecv;	// new frame received flag

	//---------------- Debugging 27.07.2016:17.00
	// apa ada masalah dengan struct buat dma?
	/*
	uchar rpxbuf[272];	// we add 2bytes padding at the end to make 4-byte boundary
	uchar gpxbuf[272];
	uchar bpxbuf[272];
	uchar ypxbuf[272];	// the resulting grey pixels
	*/
} pxBuf_t;
pxBuf_t pxBuffer;



// for fetching/storing image
volatile uchar dmaImgFromSDRAMdone;


typedef struct btree {
  short p;                  // parent node
  short c[2];               // childred nodes
  uchar isLeaf;             // 1 if the node is a leaf, 0 otherwise
  // for leadAp only:
  uint  maxHistMCPLItem;    // should equal (nCoresForPixelPreProc-1)*256
  uint MCPLItemCntr;
  uchar maxHistSDPItem;     // should equal num_of_childre*4
  uchar SDPItemCntr;
} btree_t;

// for performance measurement
typedef struct meas {
	uint tCore;                 // measuring edge detection for each core
	uint tNode;				// measuring edge detection for a node
	uint tTotal;
} meas_t;
volatile uint64 tic, toc;
volatile ushort elapse;
meas_t perf;

typedef struct send_result_info {
	ushort blockToSend;					// it is used in sendResult()
	int szBlock;
	uint nReceived_MCPL_SEND_PIXELS;
	uchar *pxBufPtr;
	// regarding buffering mechanism
	ushort cl;					// current line
} send_result_info_t;
send_result_info_t sendResultInfo;

/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/
/*-------------------------- Global/Static Variables --------------------------------*/


// image/frame size
uchar pxFwdr_wID;
ushort wImg;
ushort hImg;



// flags for communication between nodes
volatile uchar flag_SendResultCont;			// wait the "continue" signal from root-node

// newImageFlag will be set on in the beginning or when the host send something via
// SDP_PORT_FRAME_END, and it will be set off when any pixel arrives.
// newImageFlag will be usedful to indicate if the new image just arrives, for example
// for computing the histogram
uchar newImageFlag;

// histogram-related data
uint maxHistValue;
uint hist[256];
uint child1hist[256];
uint child2hist[256];
btree_t histPropTree;


// SDP containers and related stuffs
sdp_msg_t resultMsg;			// prepare the result data
sdp_msg_t histMsg;              // for propagating histogram data
uint sdpDelayFactorSpin;

block_info_t *blkInfo;			// general frame info stored in sysram, to be shared with workers
w_info_t workers;				// specific info each core should hold individually
uchar needSendDebug;
uint myCoreID;
uint myChipID;
uint dmaTID;


// to speed up, let's allocate these two buffers in computeWLoad()
uchar *dtcmImgBuf;
uchar *resImgBuf;				// CHECK: make sure this is 4-byte aligned to avoid corruption!
ushort pixelCntr;				// how many pixel has been processed?
uchar *dtcmImgFilt;				// similar with dtcmImgBuf, but fixed to 5 block instead of 3 block
								// in dtcmImgBuf, the block might be 3 (SOBEL) or 5 (LAPLACE)



/*------------------------- Forward declarations ----------------------------*/

// Initialization
void initRouter();
void initSDP();
void initIPTag();
void initImgBufs();
void initHistData(uint arg0, uint arg1);    // will be called by computeWLoad(), contains construction of HistPropTree

// Event handlers
void hMC(uint key, uint None);
void hMCPL(uint key, uint payload);
void hDMA(uint tag, uint tid);
void hSDP(uint mBox, uint port);

// Misc. functions
void distributeWID(uint arg0, uint arg1);
void computeWload(uint szFrame, uint arg1);
void configure_network(uint mBox);


uchar get_Nworkers();			// leadAp might want to know, how many workers are active


// memory management
void releaseImgBuf();
void allocateImgBuf();		// allocate image buffers in SDRAM, called for every new frame
void allocateDtcmImgBuf();	// allocate image buffers in DTCM, called just once (during config only)

void processGrayScaling(uint arg0, uint arg1);
void recvFwdImgData(uint key, uint payload);
void collectGrayPixels(uint arg0, uint arg1);

void computeHist(uint arg0, uint arg1);     // will use ypxbuf to compute the histogram



// sending result: see frameio.c
void sendResult(uint blkID, uint arg1);		// blkID is the expected node to send
void sendResultChain(uint nextBlock, uint unused);
uint getSdramResultAddr();
uint getSdramBlockResultAddr();	// similar to getSdramResultAddr but used ONLY by root-node
void worker_send_result(uint arg0, uint arg1);
void worker_recv_result(uint arg0, uint arg1);



// debugging and reporting
volatile uint giveDelay(uint delVal);	// delVal is the number of clock step
void hTimer(uint tick, uint Unused);
void getTaskName(proc_t proc, char strBuf[]);

REAL roundr(REAL inVal);

#endif // SPINNVID_H

