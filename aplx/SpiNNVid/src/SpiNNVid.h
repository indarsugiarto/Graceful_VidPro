#ifndef SPINNVID_H
#define SPINNVID_H

#include <spin1_api.h>

#define MAJOR_VERSION			0
#define MINOR_VERSION			1

#define USING_SPIN				5	// 3 for spin3, 5 for spin5

#define DEBUG_LEVEL				1

// Basic spin1_api related
#define PRIORITY_LOWEST         4
#define PRIORITY_TIMER			3
#define PRIORITY_PROCESSING		2
#define PRIORITY_SDP			1
#define PRIORITY_DMA			0
#define PRIORITY_MCPL			-1

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
#define SDP_PORT_ACK			6
#define SDP_PORT_CONFIG			7	// for sending image/frame info
#define SDP_CMD_CONFIG			1	// will be sent via SDP_PORT_CONFIG
#define SDP_CMD_CONFIG_CHAIN	11
#define SDP_CMD_PROCESS			2	// will be sent via SDP_PORT_CONFIG
#define SDP_CMD_CLEAR			3	// will be sent via SDP_PORT_CONFIG
#define SDP_CMD_ACK_RESULT		4


#define SPINNVID_APP_ID			16

// Where will we put the frames
#define IMG_R_BUFF0_BASE		0x61000000	// for storing R-channel
#define IMG_G_BUFF0_BASE		0x62000000	// for storing G-channel
#define IMG_B_BUFF0_BASE		0x63000000	// for storing B-channel
#define IMG_O_BUFF1_BASE		0x64000000	// the result
#define IMG_O_BUFF2_BASE		0x65000000	// optional: result after filtering
#define IMG_O_BUFF3_BASE		0x66000000	// optional:

// We use timer to
#define TIMER_TICK_PERIOD_US 	1000000

// Multicast packet key definition
//we also has direct key to each core (see initRouter())
#define MCPL_BCAST_INFO_KEY			0xbca50001	// for broadcasting ping and blkInfo
#define MCPL_BCAST_CMD_FILT			0xbca50002	// command for filtering only
#define MCPL_BCAST_CMD_DETECT		0xbca50003  // command for edge detection
#define MCPL_BCAST_GET_WLOAD		0xbca50004	// trigger workers to compute workload
#define MCPL_PING_REPLY				0x1ead0001
#define MCPL_FILT_DONE				0x1ead0002	// worker send signal to leader
#define MCPL_EDGE_DONE				0x1ead0003
#define MCPL_BCAST_SEND_RESULT		0xbca50005	// broadcasting host acknowledge
#define MCPL_BCAST_HOST_ACK			0xbca50006

// special key with base value 0xbca5FFF
#define MCPL_BCAST_PIXEL_BASE		0xbca60000
#define MCPL_BCAST_RED_PX			0xbca60001  // the pixels are in the payload
#define MCPL_BCAST_RED_PX_END		0xbca60002  // notify next node that image channel is complete
#define MCPL_BCAST_GREEN_PX			0xbca60003  // FFF part contains information about dLen
#define MCPL_BCAST_GREEN_PX_END		0xbca60004
#define MCPL_BCAST_BLUE_PX			0xbca60005
#define MCPL_BCAST_BLUE_PX_END		0xbca60006
#define MCPL_BCAST_IMG_READY		0xbca6FFF7

// special key for forwarding image configuration
#define MCPL_BCAST_IMG_INFO_BASE	0xbca70000	// start of info chain
#define MCPL_BCAST_IMG_INFO_SIZE	0xbca70001
#define MCPL_BCAST_IMG_INFO_NODE	0xbca70002
#define MCPL_BCAST_IMG_INFO_OPR		0xbca70003
#define MCPL_BCAST_IMG_INFO_EOF		0xbca70004	// end of info

//special key (with values)
#define MCPL_BLOCK_DONE			0x1ead1ead	// should be sent to <0,0,1>


// block info
typedef struct block_info {
	ushort wImg;
	ushort hImg;
	ushort isGrey;			// 0==color, 1==gray
	uchar opType;			// 0==sobel, 1==laplace
	uchar opFilter;			// 0==no filtering, 1==with filtering
	ushort nodeBlockID;		// will be send by host
	ushort maxBlock;		// will be send by host
	uchar Nworkers;			// number of workers
	// then pointers to the image in SDRAM
	uchar *imgRIn;
	uchar *imgGIn;
	uchar *imgBIn;
	uchar *imgROut;
	uchar *imgGOut;
	uchar *imgBOut;
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
	// helper pointers
	ushort wImg;		// just a copy of block_info_t.wImg
	ushort hImg;		// just a copy of block_info_t.hImg
	uchar *imgRIn;		// each worker has its own value of imgRIn --> workload base
	uchar *imgGIn;		// idem
	uchar *imgBIn;		// idem
	uchar *imgOut1;		// idem
	uchar *imgOut2;		// idem
	uchar *imgOut3;		// idem
	uchar *blkImgRIn;	// this will be shared among cores in the same chip
	uchar *blkImgGIn;	// idem
	uchar *blkImgBIn;	// idem
	uchar *blkImgOut1;	// idem
	uchar *blkImgOut2;	// idem
	uchar *blkImgOut3;	// idem
} w_info_t;

typedef struct chain {
	ushort x;
	ushort y;
	ushort id;

} chain_t;

ushort nodeCntr;				// to count, how many non-root nodes are present/active

block_info_t *blkInfo;			// general frame info stored in sysram, to be shared with workers
w_info_t workers;				// specific info each core should hold individually
uchar needSendDebug;
uint myCoreID;


// SDP containers
sdp_msg_t replyMsg;				// prepare the reply message
sdp_msg_t resultMsg;			// prepare the result data
sdp_msg_t debugMsg;				// and the debug data


/*------------------------- Forward declarations ----------------------------*/

// Initialization
void initRouter();
void initSDP();
void initImage();

// Event handlers
void hMCPL(uint key, uint payload);
void hDMA(uint tag, uint tid);
void hSDP(uint mBox, uint port);

// Misc. functions
void initCheck();
uchar get_Nworkers();			// leadAp might want to know, how many workers are active
uchar get_block_id();			// get the block id, given the number of chips available
void initIDcollection(uint withBlkInfo, uint Unused);
void bcastWID(uint Unused, uint null);
void printWID(uint None, uint Neno);
#endif // SPINNVID_H

