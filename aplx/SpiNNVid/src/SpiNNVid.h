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
#define REAL                    accum
#define REAL_CONST(x)           x##k


// forward RGB or just gray pixels?
// forwarding RGB is slower, but might be useful for general image processing
// in the future
#define FWD_FULL_COLOR          FALSE

#define MAJOR_VERSION           0
#define MINOR_VERSION           2

// Version log
// 0.1 Sending frame to SpiNNaker at 10MBps
// 0.2 Do the edge detection


//#define USING_SPIN				5	// 3 for spin3, 5 for spin5

// if USE_FIX_NODES is used, the node configuration MUST be supplied in the beginning
#define USE_FIX_NODES               // use 4 chips or 48 chips

#if(USING_SPIN==3)
#define MAX_NODES               4
#elif(USING_SPIN==5)
#define MAX_NODES               48
#endif


// all constants below must be positive
#define R_GRAY                  REAL_CONST(0.2989)
#define G_GRAY                  REAL_CONST(0.5870)
#define B_GRAY                  REAL_CONST(0.1140)

#define DEST_HOST			1
#define DEST_FPGA			2
#define DESTINATION			DEST_HOST

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
static const short FILT[5][5] = {{2,4,5,4,2},
				   {4,9,12,9,4},
				   {5,12,15,12,5},
				   {4,9,12,9,4},
				   {2,4,5,4,2}};
static const short FILT_DENOM = 159;


/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/
/*--------------------- Debugging and reporting definition --------------------------*/
//#define DEBUG_LEVEL				0	// no debugging message at all
#define DEBUG_LEVEL				1
// various report for debugging (send by host):
#define DEBUG_REPORT_NWORKERS	1		// only leadAp
#define DEBUG_REPORT_WID		2		// only leadAp
#define DEBUG_REPORT_BLKINFO	3		// only leadAp
#define DEBUG_REPORT_MYWID		4		// all cores
#define DEBUG_REPORT_WLOAD		5		// all cores, report via tag-3

// We use timer to do some debugging facilities
#define TIMER_TICK_PERIOD_US 	1000000


/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/
/*----------------------------- Basic spin1_api related -----------------------------*/
#define PRIORITY_LOWEST         4
#define PRIORITY_TIMER			3
#define PRIORITY_PROCESSING		2
#define PRIORITY_SDP			1
#define PRIORITY_DMA			0
#define PRIORITY_MCPL			-1

#define DEF_SDP_TIMEOUT			10			// datasheet says 10 is enough
#define SDP_TAG_REPLY			1
#define SDP_UDP_REPLY_PORT		20001
#define SDP_HOST_IP				0x02F0A8C0	// 192.168.240.2, dibalik!
#define SDP_TAG_RESULT			2
#define SDP_UDP_RESULT_PORT		20002
#define SDP_TAG_DEBUG           3
#define SDP_UDP_DEBUG_PORT      20003

#define SDP_PORT_R_IMG_DATA		1	// port for sending R-channel
#define SDP_PORT_G_IMG_DATA		2	// port for sending G-channel
#define SDP_PORT_B_IMG_DATA		3	// port for sending B-channel
#define SDP_PORT_FRAME_END      4   // instruct spinn to start decoding
#define SDP_PORT_FRAME_INFO     5   // will be used to send the basic info about frame
#define SDP_PORT_ACK			6
#define SDP_PORT_CONFIG			7	// for sending image/frame info

//#define SDP_CMD_CONFIG			1	// will be sent via SDP_PORT_CONFIG
#define SDP_CMD_CONFIG_NETWORK  1   // for setting up the network
#define SDP_CMD_GIVE_REPORT		2

//#define SDP_CMD_CONFIG_CHAIN	11  // maybe we don't need it?
#define SDP_CMD_PROCESS			3	// will be sent via SDP_PORT_CONFIG
#define SDP_CMD_CLEAR			4	// will be sent via SDP_PORT_CONFIG
#define SDP_CMD_ACK_RESULT		5


#define SPINNVID_APP_ID			16

// From my experiment with test_sdp_stream_to_host:
#define DEF_DEL_VAL     900  // perfect, up to 5.7MBps


// dma transfer
//#define DMA_TAG_STORE_FRAME		10
#define DMA_TAG_STORE_R_PIXELS	20
#define DMA_TAG_STORE_G_PIXELS	40
#define DMA_TAG_STORE_B_PIXELS	60
#define DMA_TAG_STORE_Y_PIXELS	80

#define DMA_FETCH_IMG_TAG		100
#define DMA_STORE_IMG_TAG		120


/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/
/*-------------------------- Parallel Processing Stuffs -----------------------------*/
// Multicast packet key definition
// we also has direct key to each core (see initRouter())

// mechanism for discovery:
#define MCPL_BCAST_INFO_KEY			0xbca50001	// for broadcasting ping and blkInfo
#define MCPL_PING_REPLY				0x1ead0001

// mechanism for broadcasting info
#define MCPL_BCAST_OP_INFO          0xbca50002  // filtering & operator types
#define MCPL_BCAST_NODES_INFO       0xbca50003
#define MCPL_BCAST_GET_WLOAD		0xbca50004	// trigger workers to compute workload
#define MCPL_BCAST_FRAME_INFO		0xbca50005
#define MCPL_BCAST_ALL_REPORT		0xbca50006	// the payload might contain specific reportType
#define MCPL_BCAST_START_PROC		0xbca50007
#define MCPL_BCAST_SEND_RESULT		0xbca50008	// trigger the node to send the result to dest

// mechanism for controlling the image processing
#define MCPL_EDGE_DONE				0x1ead0003

//special key (with values)
#define MCPL_BLOCK_DONE			0x1ead1ead	// should be sent to <0,0,1>


// mechanism for forwarding pixel packets
// important: MCPL_FWD_PIXEL_INFO must preceed all chunk pixels!!!
#define MCPL_FWD_PIXEL_INFO			0xbca60000	// broadcast pixel chunk information
// the payload of MCPL_FWD_PIXEL_INFO will contain:
// xxxxyyyy, where xxxx = pxLen, yyyy = pxSeq -> see pxBuf_t below !!!
#define MCPL_FWD_PIXEL_RDATA		0xbca70000	// broadcast pixel chunk data for R-ch
#define MCPL_FWD_PIXEL_GDATA		0xbca80000	// broadcast pixel chunk data for G-ch
#define MCPL_FWD_PIXEL_BDATA		0xbca90000	// broadcast pixel chunk data for B-ch
#define MCPL_FWD_PIXEL_YDATA		0xbcaA0000	// broadcast pixel chunk data for B-ch
#define MCPL_FWD_PIXEL_EOF			0xbcaB0000	// end of transfer, start grayscaling!
#define MCPL_FWD_PIXEL_MASK			0xFFFF0000	// lower word is used for core-ID




/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/
/*-------------------------- Image Processing Stuffs --------------------------------*/
// Where will we put the frames
#define IMG_R_BUFF0_BASE		0x61000000	// for storing R-channel
#define IMG_G_BUFF0_BASE		0x62000000	// for storing G-channel
#define IMG_B_BUFF0_BASE		0x63000000	// for storing B-channel
#define IMG_O_BUFF1_BASE		0x64000000	// the result
#define IMG_O_BUFF2_BASE		0x65000000	// optional: result after filtering
#define IMG_O_BUFF3_BASE		0x66000000	// optional:

#define IMG_SOBEL				0
#define IMG_LAPLACE				1
#define IMG_WITHOUT_FILTER		0
#define IMG_WITH_FILTER			1

/*------------------------ Struct, Enum, Type definition ----------------------------*/
// block info
typedef struct block_info {
	ushort wImg;
	ushort hImg;
	//ushort isGrey;			// 0==color, 1==gray
	uchar opType;			// 0==sobel, 1==laplace
	uchar opFilter;			// 0==no filtering, 1==with filtering
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
	uint cntPixel;			// how many pixels will be fetch by dtcmImgBuf?
	// helper pointers
	ushort wImg;		// just a copy of block_info_t.wImg
	ushort hImg;		// just a copy of block_info_t.hImg
	uchar opType;			// 0==sobel, 1==laplace
	uchar opFilter;			// 0==no filtering, 1==with filtering

	uchar *imgRIn;		// each worker has its own value of imgRIn --> workload base
	uchar *imgGIn;		// idem
	uchar *imgBIn;		// idem
	uchar *imgOut1;		// idem
	uchar *imgOut2;		// idem
	uchar *imgOut3;		// idem

	// NOTE: only leadAp needs the following variables (for sending result to host)
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
#define DEF_PXLEN_IN_CHUNK	270
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

uchar nFiltJobDone;				// will be used to count how many workers have
uchar nEdgeJobDone;				// finished their job in either filtering or edge detection
uchar nBlockDone;

// to speed up, let's allocate these two buffers in computeWLoad()
uchar *dtcmImgBuf;
uchar *resImgBuf;
ushort pixelCntr;				// how many pixel has been processed?

// for performance measurement
volatile uint64 tic, toc;
volatile ushort elapse;


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

