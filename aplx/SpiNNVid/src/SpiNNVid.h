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
#include "defSpiNNVid.h"        // all definitions go here

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
/*
static const uchar FILT[5][5] = {{2,4,5,4,2},
				   {4,9,12,9,4},
				   {5,12,15,12,5},
				   {4,9,12,9,4},
				   {2,4,5,4,2}};
static const uchar FILT_DENOM = 159;
*/
// based on: http://homepages.inf.ed.ac.uk/rbf/HIPR2/gsmooth.htm

static const short FILT[5][5] = {{1,4,7,4,1},
				   {4,16,26,16,4},
				   {7,26,41,26,7},
				   {4,16,26,16,4},
				   {1,4,7,4,1}};
static const short FILT_DENOM = 273;

/*
static const uchar FILT[5][5] = {1};
static const uchar FILT_DENOM = 25;
*/
/*------------------------ Struct, Enum, Type definition ----------------------------*/
// block info
typedef struct block_info {
	ushort wImg;
	ushort hImg;
	//ushort isGrey;			// 0==color, 1==gray
	uchar opType;			// 0==no operation, 1==sobel, 2==laplace, 3==dvs
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
	//uchar *imgOut2;         // will be used if primary output is for filtering
	//uchar *imgOut3;         // will be used if imgOut2 is used (for sharpening)

	// miscellaneous flag
	volatile uchar dmaToken_pxStore;	// token for storing pixel to SDRAM via dma
										// the value indicates: who's next?
	volatile uchar dmaDone_rpxStore;	// who has completed the dma for R-chunk?
	volatile uchar dmaDone_gpxStore;	// and for G-chunk?
	volatile uchar dmaDone_bpxStore;	// and for B-chunk?
	volatile uchar dmaDone_ypxStore;	// and for Y-chunk?
} block_info_t;

// worker info
typedef struct w_info {
	uchar wID[17];			// coreID of all workers (max is 17), hold by leadAp
	uchar subBlockID;		// this will be hold individually by each worker
	uchar tAvailable;		// total available workers, should be initialized to 1
	// per-block info:
	ushort blkStart;		// this is per block (it differs from startLine)
	ushort blkEnd;			// (it differs from endLine)
	ushort nLinesPerBlock;
	// per-core info:
	ushort startLine;		// this is per core (it differs from blkStart)
	ushort endLine;			// it differs from blkStart
	uint szDtcmImgBuf;			// how many pixels will be fetch by dtcmImgBuf?
	uint szDtcmImgFilt;		// similar to szDtcmImgBuf, but for dtcmImgFilt
	uchar active;			// if nLinesPerBlock > tAvailable, this will be on,
	// helper pointers
	ushort wImg;		// just a copy of block_info_t.wImg
	ushort hImg;		// just a copy of block_info_t.hImg
	uchar opType;			// 0==no op, 1==sobel, 2==laplace, 3==dvs
	uchar opFilter;			// 0==no filtering, 1==with filtering
	uchar opSharpen;		// 0==no sharpening, 1==with sharpening

	uchar *imgRIn;		// each worker has its own value of imgRIn --> workload base
	uchar *imgGIn;		// idem
	uchar *imgBIn;		// idem
	uchar *imgOut1;		// idem
	//uchar *imgOut2;		// idem
	//uchar *imgOut3;		// idem

	// NOTE: only leadAp needs the following variables (for sending result to host)
	uchar tRunning;			// the actual number of currently involved workers
	uchar *blkImgRIn;	// this will be shared among cores in the same chip
	uchar *blkImgGIn;	// will be used when sending report to host-PC
	uchar *blkImgBIn;	// hence, only leadAp needs it
	uchar *blkImgOut1;	// idem
	//uchar *blkImgOut2;	// idem
	//uchar *blkImgOut3;	// idem

	// since there are different operation, it is better if we track the image address
	uchar currentTask;
	uchar *sdramImgIn;		// sdramImgIn and sdramImgOut might be interchanged
	uchar *sdramImgOut;		// during process switching
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


// Coba cara baru, multiple cores receive frames consecutively. This way, each core
// will have enough time to do grayscaling, histogram, storing/broadcasting.
// NOTE: pxSeq is the chunk seqment and is contained in the tag and srce_port in
//		 the SDP data sent to root-node.
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
// these are initialized in the SpiNNVid_main()
uchar *rpxbuf;
uchar *gpxbuf;
uchar *bpxbuf;
uchar *ypxbuf;

// for fetching/storing image
volatile uchar dmaImgFromSDRAMdone;

enum proc_type_e
{
  PROC_NONE,
  PROC_FILTERING,		// Filtering a.k.a. smoothing
  PROC_SHARPENING,		// using Histogram equalization
  PROC_EDGING_DVS,	 	// Edge detection or DVS emulation
  PROC_SEND_RESULT
};

typedef enum proc_type_e proc_t;	//!< Typedef for enum spin_lock_e

proc_t proc;


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

// regarding task list
typedef struct task_list {
	proc_t tasks[5];			// maximum number of tasks is 5
	uint tPerf[5];				// time measurement of the above tasks[]
	uchar nTasks;
	proc_t cTask;				// current task
	uchar cTaskPtr;				// current task pointer
	int EOF_flag;				// SDP_PORT_FRAME_END flag for fault tolerance via redundancy
								// it contains the frame-ID sent by the host-PC
} task_list_t;
task_list_t taskList;

// regarding sending result, the root-node needs to track the current requested line number:
#define MIN_ADAPTIVE_DELAY		150	// I found this is good enough
//#define MCPL_DELAY_FACTOR		1	// works OK with small image
#define MCPL_DELAY_FACTOR		1
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

// are we running in release mode or debug mode?
char signature[15];	// Will display [SpiNNVid] or [SpiNNVid-dbg-x]

// how many chips are responding
uint nChipAlive;
ushort aliveNodeAddr[MAX_NODES];

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
sdp_msg_t replyMsg;				// prepare the reply message
sdp_msg_t resultMsg;			// prepare the result data
sdp_msg_t debugMsg;				// and the debug data
sdp_msg_t histMsg;              // for propagating histogram data
uint sdpDelayFactorSpin;

//ushort nodeCntr;				// to count, how many non-root nodes are present/active
chain_t chips[MAX_NODES];		// the value of MAX_NODES depends on whether Spin3(4) or Spin5(48)

block_info_t *blkInfo;			// general frame info stored in sysram, to be shared with workers
w_info_t workers;				// specific info each core should hold individually
uchar needSendDebug;
uint myCoreID;
uint myChipID;
uint dmaTID;

// nFiltJobDone and nEdgeJobDone are deprecated!!!
//volatile uchar nFiltJobDone;				// will be used to count how many workers have
//volatile uchar nEdgeJobDone;				// finished their job in either filtering or edge detection
volatile uchar nWorkerDone;
volatile uchar nBlockDone;

// to speed up, let's allocate these two buffers in computeWLoad()
uchar *dtcmImgBuf;
uchar *resImgBuf;				// CHECK: make sure this is 4-byte aligned to avoid corruption!
ushort pixelCntr;				// how many pixel has been processed?
uchar *dtcmImgFilt;				// similar with dtcmImgBuf, but fixed to 5 block instead of 3 block
								// in dtcmImgBuf, the block might be 3 (SOBEL) or 5 (LAPLACE)



uchar nCoresForPixelPreProc;

/*------------------------- Forward declarations ----------------------------*/

// Initialization
void initRouter();
void initSDP();
void initIPTag();
void initOther();
void initImgBufs();
void initHistData(uint arg0, uint arg1);    // will be called by computeWLoad(), contains construction of HistPropTree
void terminate_SpiNNVid(char *stream, char *msg, uint exitCode);

// Event handlers
void hMC_SpiNNVid(uint key, uint None);
void hMCPL_SpiNNVid(uint key, uint payload);
void hDMA(uint tag, uint tid);
void hSDP(uint mBox, uint port);

// Misc. functions
void initCheck();
uchar get_Nworkers();			// leadAp might want to know, how many workers are active
uchar get_def_Nblocks();
uchar get_block_id();			// get the block id, given the number of chips available
void getChipXYfromID(ushort id, ushort *X, ushort *Y);

// processing: worker discovery
void notifyTaskDone();
void taskProcessingLoop(uint frameID, uint arg1);
void initIDcollection(uint withBlkInfo, uint Unused);
void bcastWID(uint Unused, uint null);

void computeWLoad(uint withReport, uint arg1);

// memory management
void releaseImgBuf();
void allocateImgBuf();		// allocate image buffers in SDRAM, called for every new frame
void allocateDtcmImgBuf();	// allocate image buffers in DTCM, called just once (during config only)

void fwdImgData(uint arg0, uint arg1);	// isLastChannel==1 for B-channel
void processGrayScaling(uint arg0, uint arg1);
void recvFwdImgData(uint key, uint payload);
void collectGrayPixels(uint arg0, uint arg1);

void computeHist(uint arg0, uint arg1);     // will use ypxbuf to compute the histogram

void triggerProcessing(uint taskID, uint arg1);



// core image processing: see process.c
void imgFiltering(uint arg0, uint arg1);
void imgSharpening(uint arg0, uint arg1);
void imgDetection(uint arg0, uint arg1);


// sending result: see frameio.c
void sendResult(uint blkID, uint arg1);		// blkID is the expected node to send
void sendResultChain(uint nextBlock, uint unused);
uint getSdramResultAddr();
uint getSdramBlockResultAddr();	// similar to getSdramResultAddr but used ONLY by root-node
void worker_send_result(uint arg0, uint arg1);
void worker_recv_result(uint arg0, uint arg1);



// debugging and reporting
void give_report(uint reportType, uint target);
volatile uint giveDelay(uint delVal);	// delVal is the number of clock step
void hTimer(uint tick, uint Unused);
/*
void seePxBuffer(char *stream);
void peekPxBufferInSDRAM(char *stream);
*/
void getTaskName(proc_t proc, char strBuf[]);

REAL roundr(REAL inVal);

/*--------- We change the algorithm into multiple cores processing for frames ---------
void storeDtcmImgBuf(uint ch, uint Unused);
void processImgData(uint mBox, uint channel);
void decompress(uchar channel);		// we found it useless!!!
-------------------------------------------------------------------------------------*/

#endif // SPINNVID_H

