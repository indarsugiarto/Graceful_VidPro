/* For converting rgb to gray, we have to use stdfix.
 * Here, we have to define the weight for the conversion. For simplicity, we can
 * use matlab version:
 * 0.2989 * R + 0.5870 * G + 0.1140 * B
 * (http://uk.mathworks.com/help/matlab/ref/rgb2gray.html)
 * */
#ifndef SPINNVID_H
#define SPINNVID_H

/*--------- For experiment ---------*/
#define SPECIAL_CORE_TO_REPORT_T_EDGE    1


#include <spin1_api.h>
#include <stdfix.h>
#include "defSpiNNVid.h"        // all definitions go here
#include "profiler.h"

/*------------- From timer2 code from Steve Temple -------------*/
// Use "timer2" to measure elapsed time.
// Times up to around 10 sec should be OK.

// Enable timer - free running, 32-bit
#define ENABLE_TIMER() tc[T2_CONTROL] = 0x82

// To measure, set timer to 0
#define START_TIMER() tc[T2_LOAD] = 0

// It produces the clock cycle
#define READ_TIMER() (0 - tc[T2_COUNT])
/*--------------------------------------------------------------*/


/* 3x3 GX and GY Sobel mask.  Ref: www.cee.hw.ac.uk/hipr/html/sobel.html */
static const short GX[3][3] = {{-1,0,1},
				 {-2,0,2},
				 {-1,0,1}};
static const short GY[3][3] = {{1,2,1},
				 {0,0,0},
				 {-1,-2,-1}};

/* Laplace operator: 5x5 Laplace mask.  Ref: Myler Handbook p. 135 */
static const short LAP[5][5] = {{-1,-1,-1,-1,-1},
				  {-1,-1,-1,-1,-1},
				  {-1,-1,24,-1,-1},
				  {-1,-1,-1,-1,-1},
				  {-1,-1,-1,-1,-1}};

/* Gaussian filter. Ref:  en.wikipedia.org/wiki/Canny_edge_detector */
// with sigma  = 1.4
static const short FILT[5][5] = {{2,4,5,4,2},
				   {4,9,12,9,4},
				   {5,12,15,12,5},
				   {4,9,12,9,4},
				   {2,4,5,4,2}};
static const short FILT_DENOM = 159;



/*------------------------ Struct, Enum, Type definition ----------------------------*/
// block info
typedef struct block_info {
	ushort wImg;
	ushort hImg;
	//ushort isGrey;			// 0==color, 1==gray
	uchar opType;			// 0==sobel, 1==laplace
	uchar opFilter;			// 0==no filtering, 1==with filtering
	uchar opSharpen;		// 0==no sharpening, 1==with sharpening
	uchar nodeBlockID;		// will be send by host
	uchar myX;				// == CHIP_X(sv->p2p_addr)
	uchar myY;				// == CHIP_Y(sv->p2p_addr)
	uchar maxBlock;		// will be send by host
	uchar Nworkers;			// number of workers, will be collected using get_Nworkers()
	// then pointers to the image in SDRAM
	uchar *imgRIn;
	uchar *imgGIn;
	uchar *imgBIn;
	// uchar *imgROut;      // old version
	// uchar *imgGOut;
	// uchar *imgBOut;
	uchar *imgOut1;         // to hold primary output
	uchar *imgOut2;         // will be used if primary output is for filtering
	uchar *imgOut3;         // additional output
	// miscellaneous info
	uchar imageInfoRetrieved;
	uchar fullRImageRetrieved;
	uchar fullGImageRetrieved;
	uchar fullBImageRetrieved;
} block_info_t;

// worker info
typedef struct w_info {
	uchar wID[17];			// coreID of all workers (max is 17), hold by leadAp
	uchar subBlockID;		// this will be hold individually by each worker
	uchar tAvailable;		// total available workers, should be initialized to 1
	ushort blkStart;
	ushort blkEnd;
	ushort nLinesPerBlock;
	ushort startLine;
	ushort endLine;
	uint szDtcmImgBuf;			// how many pixels will be fetch by dtcmImgBuf?
	uchar active;			// if nLinesPerBlock > tAvailable, this will be on,
	// helper pointers
	ushort wImg;		// just a copy of block_info_t.wImg
	ushort hImg;		// just a copy of block_info_t.hImg
	uchar opType;			// 0==sobel, 1==laplace
	uchar opFilter;			// 0==no filtering, 1==with filtering
	uchar opSharpen;		// 0==no sharpening, 1==with sharpening

	uchar *imgRIn;		// each worker has its own value of imgRIn --> workload base
	uchar *imgGIn;		// idem
	uchar *imgBIn;		// idem
	uchar *imgOut1;		// idem
	uchar *imgOut2;		// idem
	uchar *imgOut3;		// idem

	// NOTE: only leadAp needs the following variables (for sending result to host)
	uchar tRunning;			// the actual number of currently involved workers
	uchar *blkImgRIn;	// this will be shared among cores in the same chip
	uchar *blkImgGIn;	// will be used when sending report to host-PC
	uchar *blkImgBIn;	// hence, only leadAp needs it
	uchar *blkImgOut1;	// idem
	uchar *blkImgOut2;	// idem
	uchar *blkImgOut3;	// idem
} w_info_t;

typedef struct chain {
	ushort x;
	ushort y;
	ushort id;

} chain_t;


typedef struct fwdPkt {
	uchar pxInfo[272];
	ushort pxLen;
} fwdPkt_t;

fwdPkt_t fwdPktBuffer[3];	// for each channel



// Coba cara baru, multiple cores receive frames direcly
typedef struct pxBuf {
	ushort pxSeq;	// the segment index of the chunk within the overall frame structure
	ushort pxLen;	// how many pixels are contained in one channel chunk
	ushort pxCntr[4];	// for receiver only, to track which pixels are broadcasted
	ushort pad;		// apa ini? tanpa ini, muncul dua byte "00 00" di awal
	/*
	uchar rpxbuf[270];	// 270/4 = 67 with 2 bytes remaining
	uchar gpxbuf[270];
	uchar bpxbuf[270];
	*/

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

// Ada masalah jika buffer dimasukkan ke dalam struct. Coba jika ditaruh di luar:
// Ternyata masih bermasalah. Sekarang mari kota coba dengan benar-benar membuat
// berada di boundary 4-byte.
#define DEF_PXLEN_IN_CHUNK	272
/*
uchar rpxbuf[DEF_PXLEN_IN_CHUNK];	// 270/4 = 67 with 2 bytes remaining
uchar gpxbuf[DEF_PXLEN_IN_CHUNK];
uchar bpxbuf[DEF_PXLEN_IN_CHUNK];
uchar ypxbuf[DEF_PXLEN_IN_CHUNK];
/*
uchar rpxbuf[270];	// 270/4 = 67 with 2 bytes remaining
uchar gpxbuf[270];
uchar bpxbuf[270];
uchar ypxbuf[270];
*/

// Coba ?pxbuf di taruh di heap
uchar *rpxbuf;
uchar *gpxbuf;
uchar *bpxbuf;
uchar *ypxbuf;

// for fetching/storing image
volatile uchar dmaImgFromSDRAMdone;

enum proc_type_e
{
  PROC_FILTERING,		// Filtering a.k.a. smoothing
  PROC_HISTEQ,		 	// Histogram equalization a.k.a. sharpening
  PROC_EDGING		 	// Edge detection
};

typedef enum proc_type_e proc_t;	//!< Typedef for enum spin_lock_e

proc_t proc;

/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/
/*-------------------------- Global/Static Variables --------------------------------*/

// SDP containers
sdp_msg_t replyMsg;				// prepare the reply message
sdp_msg_t resultMsg;			// prepare the result data
sdp_msg_t debugMsg;				// and the debug data


//ushort nodeCntr;				// to count, how many non-root nodes are present/active
chain_t chips[MAX_NODES];

block_info_t *blkInfo;			// general frame info stored in sysram, to be shared with workers
w_info_t workers;				// specific info each core should hold individually
uchar needSendDebug;
uint myCoreID;
uint dmaTID;

volatile uchar nFiltJobDone;				// will be used to count how many workers have
volatile uchar nEdgeJobDone;				// finished their job in either filtering or edge detection
volatile uchar nBlockDone;

// to speed up, let's allocate these two buffers in computeWLoad()
uchar *dtcmImgBuf;
uchar *resImgBuf;
ushort pixelCntr;				// how many pixel has been processed?

// for performance measurement
typedef struct meas {
    uint tEdge;                 // measuring edge detection for each core
    uint tEdgeNode;
    uint tEdgeTotal;
} meas_t;
volatile uint64 tic, toc;
volatile ushort elapse;
meas_t perf;

/*------------------------- Forward declarations ----------------------------*/

// Initialization
void initRouter();
void initSDP();
void initImage();
void initIPTag();
void initOther();

// Event handlers
void hMCPL(uint key, uint payload);
void hDMA(uint tag, uint tid);
void hSDP(uint mBox, uint port);

// Misc. functions
void initCheck();
uchar get_Nworkers();			// leadAp might want to know, how many workers are active
uchar get_def_Nblocks();
uchar get_block_id();			// get the block id, given the number of chips available

// processing: worker discovery
void initIDcollection(uint withBlkInfo, uint Unused);
void bcastWID(uint Unused, uint null);

void computeWLoad(uint withReport, uint arg1);

void fwdImgData(uint arg0, uint arg1);	// isLastChannel==1 for B-channel
void processGrayScaling(uint arg0, uint arg1);
void recvFwdImgData(uint key, uint payload);
void collectGrayPixels(uint arg0, uint arg1);


void triggerProcessing(uint arg0, uint arg1);
void imgFiltering(uint arg0, uint arg1);
void imgDetection(uint arg0, uint arg1);
void afterProcessingDone(uint arg0, uint arg1);
void sendDetectionResult2Host(uint nodeID, uint arg1);
void sendDetectionResult2FPGA(uint nodeID, uint arg1);
void notifyDestDone(uint arg0, uint arg1);          // this is notifyHostDone() in previous version

// debugging and reporting
void give_report(uint reportType, uint target);
volatile uint giveDelay(uint delVal);
void hTimer(uint tick, uint Unused);
void seePxBuffer(char *stream);
void peekPxBufferInSDRAM(char *stream);

REAL roundr(REAL inVal);

/*--------- We change the algorithm into multiple cores processing for frames ---------
void storeDtcmImgBuf(uint ch, uint Unused);
void processImgData(uint mBox, uint channel);
void decompress(uchar channel);		// we found it useless!!!
-------------------------------------------------------------------------------------*/

#endif // SPINNVID_H

