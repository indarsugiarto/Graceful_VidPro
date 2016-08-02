#ifndef DEFSPINNVID_H
#define DEFSPINNVID_H

#define MAJOR_VERSION           0
#define MINOR_VERSION           2

// Version log
// 0.1 Sending frame to SpiNNaker at 10MBps
// 0.2 Do the edge detection


/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/
/*----------------------- Basic parameters for compilation --------------------------*/
// forward RGB or just gray pixels?
// forwarding RGB is slower, but might be useful for general image processing
// in the future
#define FWD_FULL_COLOR          FALSE

// now we have a Makefile that generates two aplx for spin3 and spin5
//#define USING_SPIN				5	// 3 for spin3, 5 for spin5

// if USE_FIX_NODES is used, the node configuration MUST be supplied in the beginning
//#define USE_FIX_NODES               // use 4 chips or 48 chips

#if(USING_SPIN==3)
#define MAX_NODES               4
#elif(USING_SPIN==5)
#define MAX_NODES               48
#endif

// related with stdfix.h
#define REAL                    accum
#define REAL_CONST(x)           x##k


// all constants below must be positive
#define R_GRAY                  REAL_CONST(0.2989)
#define G_GRAY                  REAL_CONST(0.5870)
#define B_GRAY                  REAL_CONST(0.1140)

// where will the result be sent? To Host or to FPGA board
#define DEST_HOST			1
#define DEST_FPGA			2
#define DESTINATION			DEST_HOST

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
#define PRIORITY_LOWEST                 4
#define PRIORITY_TIMER			3
#define PRIORITY_PROCESSING		2
#define PRIORITY_SDP			1
#define PRIORITY_DMA			0
#define PRIORITY_MCPL			-1

#define DEF_SEND_PORT                   17893           // tidak bisa diganti dengan yang lain

#define DEF_SDP_TIMEOUT			10		// datasheet says 10 is enough
#define SDP_TAG_REPLY			1
#define SDP_UDP_REPLY_PORT		20001
#define SDP_HOST_IP			0x02F0A8C0	// 192.168.240.2, dibalik!
#define SDP_TAG_RESULT			2
#define SDP_UDP_RESULT_PORT        	20002
#define SDP_TAG_DEBUG                   3
#define SDP_UDP_DEBUG_PORT              20003

#define SDP_PORT_R_IMG_DATA		1	// port for sending R-channel
#define SDP_PORT_G_IMG_DATA		2	// port for sending G-channel
#define SDP_PORT_B_IMG_DATA		3	// port for sending B-channel
#define SDP_PORT_FRAME_END      4   // instruct spinn to start decoding
#define SDP_PORT_FRAME_INFO     5   // will be used to send the basic info about frame
#define SDP_PORT_CONFIG			7	// for sending image/frame info

// special for sending result and notification, srce_port can be used for chunk counter
// while srce_addr can be used as line number of the image
#define SDP_SRCE_NOTIFY_PORT	0xFE	// cannot use 0xFF because of ETH

//#define SDP_CMD_CONFIG			1	// will be sent via SDP_PORT_CONFIG
#define SDP_CMD_CONFIG_NETWORK  1   // for setting up the network
#define SDP_CMD_GIVE_REPORT		2

//#define SDP_CMD_CONFIG_CHAIN	11  // maybe we don't need it?
#define SDP_CMD_PROCESS			3	// will be sent via SDP_PORT_CONFIG
#define SDP_CMD_CLEAR			4	// will be sent via SDP_PORT_CONFIG
#define SDP_CMD_ACK_RESULT		5


#define SPINNVID_APP_ID			16

// From my experiment with test_sdp_stream_to_host:
// for sending chunks to spinnaker:
#define DEF_QT_WAIT_VAL		200	// in nanosecond
#define DEF_PY_WAIT_VAL		200
// for sending chunks from spinnaker:
#define DEF_DEL_VAL		900  // perfect, up to 5.7MBps


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


#endif // DEFSPINNVID_H

