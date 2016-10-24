/* Default definitions used in the app SpiNNVid
 *
 * This file is also included by the GUI program
 */

#ifndef DEFSPINNVID_H
#define DEFSPINNVID_H

#define MAJOR_VERSION               2
#define MINOR_VERSION               2

// Version log
// 0.1 Sending frame to SpiNNaker at 10MBps
// 0.2 Do the edge detection and smoothing
// 0.3 Do histogram equalization
// 1.0 Implement video processing (filtering & sharpening are excluded)
// 1.1 Send to FPGA via FR directly (without SDP)
// 2.0 Implement task scheduling and DVS emulation
// 2.1 Implement buffering
// 2.2 Implement optimized buffering and better GUI


#define ADAPTIVE_FREQ               FALSE
#define DEF_FREQ					200
/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/
/*----------------------- Basic parameters for compilation --------------------------*/
// forward RGB or just gray pixels?
// forwarding RGB is slower, but might be useful for general image processing
// in the future
#define FWD_FULL_COLOR              FALSE

// now we have a Makefile that generates two aplx for spin3 and spin5
//#define USING_SPIN				5	// 3 for spin3, 5 for spin5
#define SPIN3_END_IP                253
#define SPIN5_END_IP                1


// if USE_FIX_NODES is used, the node configuration MUST be supplied in the beginning
//#define USE_FIX_NODES               // use 4 chips or 48 chips

#if(USING_SPIN==3)
#define MAX_NODES                   4
#elif(USING_SPIN==5)
#define MAX_NODES                   48
#endif

// related with stdfix.h
#define REAL                        accum
#define REAL_CONST(x)               (x##k)


// define, which core will take the leading role? Note, core-1 will be used
// for the profiler. TODO: check all leadAp usage!!!
#define PROF_CORE					1	// profiler
#define LEAD_CORE                   2	// lead core for SpiNNVid
#define STREAMER_CORE				17
#define N_CORE_FOR_EOF			1

// how many cores are used for pixel propagation (and histogram calculation)?
// this value MUST BE THE SAME with the number of cores used during SDP
// transfer for sending image's pixels
//#define NUM_CORES_FOR_BCAST_PIXEL   16
#define NUM_CORES_FOR_BCAST_PIXEL   15	// some nodes don't have 18 cores, but only 17
										// hence, we only use core-2 to core-16

// all constants below must be positive
#define R_GRAY                      REAL_CONST(0.2989)
#define G_GRAY                      REAL_CONST(0.5870)
#define B_GRAY                      REAL_CONST(0.1140)

// where will the result be sent? To Host or to FPGA board
#define DEST_HOST                   1
#define DEST_FPGA                   2
#define DESTINATION                 DEST_HOST

/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/
/*--------------------- Debugging and reporting definition --------------------------*/
#define DEBUG_LEVEL				0	// no debugging message at all
//#define DEBUG_LEVEL                 3
// various report for debugging (send by host):
#define DEBUG_REPORT_NWORKERS       1		// only leadAp (in all nodes)
#define DEBUG_REPORT_WID            2		// only leadAp (in all nodes)
#define DEBUG_REPORT_BLKINFO        3		// only leadAp (in all nodes)
#define DEBUG_REPORT_MYWID          4		// all cores
#define DEBUG_REPORT_WLOAD          5		// all cores, report via tag-3
#define DEBUG_REPORT_FRAMEINFO      6       // all cores
#define DEBUG_REPORT_NET_CONFIG     7       // only leadAp (in all nodes)
#define DEBUG_REPORT_PERF           8       // all cores
#define DEBUG_REPORT_PLL_INFO       9       // only leadAp (in all nodes)
#define DEBUG_REPORT_HISTPROP       10      // only leadAp <in root-node>
#define DEBUG_REPORT_IMGBUFS		11		// only leadAp (in all nodes)
#define DEBUG_REPORT_IMGBUF_IN		12		// for host, only leadAp (in all nodes)
#define DEBUG_REPORT_IMGBUF_OUT		13		// for host, only leadAp (in all nodes)
#define DEBUG_REPORT_TASKLIST		14		// only leadAp <in root-node>
#define DEBUG_REPORT_CLEAR_MEM		15		// only leadAp (in all nodes)
#define DEBUG_REPORT_EDGE_DONE		16		// only leadAp (in all nodes)

// We use timer to do some debugging facilities
#define TIMER_TICK_PERIOD_US        1000000


/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/
/*----------------------------- Basic spin1_api related -----------------------------*/
#define PRIORITY_FIQ				-1
#define PRIORITY_URGENT				0
#define PRIORITY_MCPL               PRIORITY_FIQ
#define PRIORITY_MC					PRIORITY_MCPL
#define PRIORITY_DMA                PRIORITY_URGENT
#define PRIORITY_SDP                1
#define PRIORITY_PROCESSING         2
#define PRIORITY_TIMER              3
#define PRIORITY_LOWEST             4
#define PRIORITY_IDLE               PRIORITY_LOWEST

#define DEF_SEND_PORT               17893           // tidak bisa diganti dengan yang lain

#define DEF_SDP_TIMEOUT             10				// datasheet says 10 is enough
#define SDP_TAG_REPLY               1
#define SDP_UDP_REPLY_PORT          20001
//note: if the host IP is different than 192.168.240.2, it should be specified here!!!
#define SDP_HOST_IP                 0x02F0A8C0		// 192.168.240.2, dibalik!
#define SDP_TAG_RESULT              2
#define SDP_UDP_RESULT_PORT        	20002
#define SDP_TAG_DEBUG				3
#define SDP_UDP_DEBUG_PORT			20003
#define SDP_TAG_PROFILER			4
#define SDP_UDP_PROFILER_PORT		20004

#define SDP_PORT_R_IMG_DATA			1	// port for sending R-channel
#define SDP_PORT_G_IMG_DATA			2	// port for sending G-channel
#define SDP_PORT_B_IMG_DATA			3	// port for sending B-channel
#define SDP_PORT_FRAME_INFO			4   // tell spin frame size or EOF
//#define SDP_PORT_FRAME_INFO			5   // will be used to send the basic info about frame
#define SDP_PORT_HISTO				5
#define SDP_PORT_MISC				6	// misc. functions, eq. debug, reset
#define SDP_PORT_CONFIG				7	// for sending image/frame info
#define SDP_PORT_PROFILER_RPT		7

// special for sending result and notification, srce_port can be used for chunk counter
// while srce_addr can be used as line number of the image
#define SDP_SRCE_NOTIFY_PORT		0xFE	// cannot use 0xFF because of ETH

//#define SDP_CMD_CONFIG			1	// will be sent via SDP_PORT_CONFIG
#define SDP_CMD_CONFIG_NETWORK  	1   // for setting up the network
#define SDP_CMD_GIVE_REPORT			2
#define SDP_CMD_RESET_NETWORK   	3   // will be sent so that all chips will have ID 0xFF
#define SDP_CMD_FRAME_INFO_SIZE		4
#define SDP_CMD_FRAME_INFO_EOF		5
#define SDP_CMD_REPORT_HIST     	6   // for sending histogram to parent node
#define SDP_CMD_END_VIDEO			7	// tell SpiNNaker, the video is over. Clean the memory!!!

//#define SDP_CMD_CONFIG_CHAIN	11  // maybe we don't need it?
#define SDP_CMD_PROCESS				3	// will be sent via SDP_PORT_CONFIG
#define SDP_CMD_CLEAR				4	// will be sent via SDP_PORT_CONFIG
#define SDP_CMD_ACK_RESULT			5


#define SPINNVID_APP_ID				16
#define PROFILER_APP_ID				17
#define FRAMEIO_APP_ID					18

// From my experiment with test_sdp_stream_to_host:
// for sending chunks to spinnaker:
#define DEF_QT_WAIT_VAL				200	// in nanosecond
#define DEF_PY_WAIT_VAL				200
// for sending chunks from spinnaker:
//#define DEF_DEL_VAL					300  // ok for image, not for video
//#define DEF_DEL_VAL					900  // perfect, up to 5.7MBps in 200MHz
//#define DEF_DEL_VAL					1200
#define DEF_DEL_VAL					25	// now we can configure delay factor from GUI
#define DEF_FR_DELAY				200

#define DEF_PXLEN_IN_CHUNK	272


// dma transfer
//#define DMA_TAG_STORE_FRAME		10
#define DMA_TAG_STORE_R_PIXELS		20
#define DMA_TAG_STORE_G_PIXELS		40
#define DMA_TAG_STORE_B_PIXELS		60
#define DMA_TAG_STORE_Y_PIXELS		80

#define DMA_FETCH_IMG_TAG			100
#define DMA_STORE_IMG_TAG			120


// memory allocation tag
#define XALLOC_TAG_BLKINFO			1
#define XALLOC_TAG_IMGRIN			2
#define XALLOC_TAG_IMGGIN			3
#define XALLOC_TAG_IMGBIN			4
#define XALLOC_TAG_IMGOUT1			5
#define XALLOC_TAG_IMGOUT2			6
#define XALLOC_TAG_IMGOUT3			7

/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/
/*-------------------------- Parallel Processing Stuffs -----------------------------*/
// Multicast packet key definition
// we also has direct key to each core (see initRouter())

// mechanism for discovery:
#define MCPL_BCAST_INFO_KEY			0xbca50001	// for broadcasting ping and blkInfo
#define MCPL_PING_REPLY				0x1ead0001
#define MCPL_BCAST_NET_DISCOVERY	0x1ead0002	// root-leadAp broadcast this to all chips
#define MCPL_BCAST_NET_REPLY		0x1ead0003	// leadAps respond with this

// mechanism for broadcasting info
#define MCPL_BCAST_OP_INFO          0xbca50002  // filtering & operator types
#define MCPL_BCAST_NODES_INFO       0xbca50003
#define MCPL_BCAST_GET_WLOAD		0xbca50004	// trigger workers to compute workload
#define MCPL_BCAST_FRAME_INFO		0xbca50005
#define MCPL_BCAST_ALL_REPORT		0xbca50006	// the payload might contain specific reportType
#define MCPL_BCAST_START_PROC		0xbca50007
#define MCPL_BCAST_RESET_NET        0xbca50008  // reset the network

// mechanism for controlling the image processing
#define MCPL_EDGE_DONE				0x1ead0004
#define MCPL_FILT_DONE				0x1ead0005

// special key (with values)
//#define MCPL_BLOCK_DONE			0x1ead1ead	// should be sent to <0,0,1>
//#define MCPL_BLOCK_DONE_TEDGE   0x1eaddea1 // should be sent to <0,0,1>
#define MCPL_BLOCK_DONE_TEDGE		0xdea10000	// for sending block done with info nodeID + perf
#define MCPL_BLOCK_DONE_TFILT		0xdea20000	//
#define MCPL_RECV_END_OF_FRAME		0xdea40000	// for sending SDP_PORT_FRAME_END to LEAD_CORE
#define MCPL_IGNORE_END_OF_FRAME	0xdea50000	// LEAD_CORE broadcast this because it has
												// already received SDP_PORT_FRAME_END

// special key for communication with the profiler
#define MCPL_TO_ALL_PROFILER		0x11111111	// profiler in all nodes
#define MCPL_TO_OWN_PROFILER		0x22222222	// profiler in its own node

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



// we found that using sdp, the max. throughput for other-than-root-node is 1.1MBps
// but the root node has throughput 5.7MBps

// with buffering technique:
#define MCPL_SEND_PIXELS_BLOCK		0x3a010000	// to all leadAps NOT in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES 0x3a020000	// to all workers in a chip
// the worker in other node first sends MCPL_SEND_PIXELS_BLOCK_CORES_INIT
// as MC to its partner in root-node. This key contains the line number
#define MCPL_SEND_PIXELS_BLOCK_CORES_INIT	0x3d000000	// to core in root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA	0x3b000000	// the base key
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA02	0x3b020000	// to core-2 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA03 0x3b030000	// to core-3 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA04 0x3b040000	// to core-4 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA05	0x3b050000	// to core-5 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA06 0x3b060000	// to core-6 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA07 0x3b070000	// to core-7 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA08 0x3b080000	// to core-8 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA09 0x3b090000	// to core-9 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA0A 0x3b0A0000	// to core-10 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA0B 0x3b0B0000	// to core-11 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA0C 0x3b0C0000	// to core-12 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA0D 0x3b0D0000	// to core-13 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA0E 0x3b0E0000	// to core-14 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA0F 0x3b0F0000	// to core-15 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA10 0x3b100000	// to core-16 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DATA11 0x3b110000	// to core-17 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT	0x3c000000	// the base key
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT02	0x3c020000	// to core-2 in other nodes
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT03 0x3c030000	// to core-3 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT04 0x3c040000	// to core-4 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT05	0x3c050000	// to core-5 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT06 0x3c060000	// to core-6 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT07 0x3c070000	// to core-7 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT08 0x3c080000	// to core-8 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT09 0x3c090000	// to core-9 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT0A 0x3c0A0000	// to core-10 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT0B 0x3c0B0000	// to core-11 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT0C 0x3c0C0000	// to core-12 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT0D 0x3c0D0000	// to core-13 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT0E 0x3c0E0000	// to core-14 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT0F 0x3c0F0000	// to core-15 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT10 0x3c100000	// to core-16 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_NEXT11 0x3c110000	// to core-17 in the root-node
#define MCPL_SEND_PIXELS_BLOCK_CORES_DONE	0x3dff0000	// from workers to its leadAp
#define MCPL_SEND_PIXELS_BLOCK_DONE	0x3a030000	// to leadAp-root from other leadAps
#define MCPL_SEND_PIXELS_BLOCK_MASK	0xFFFF0000
#define MCPL_SEND_PIXELS_BLOCK_PREP	0x3a040000	// leadAp-root prepares its workers
#define MCPL_SEND_PIXELS_BLOCK_INFO_STREAMER_SZIMG	0x3a050000	// tell "streamer" about image size
#define MCPL_SEND_PIXELS_BLOCK_INFO_STREAMER_DEL	0x3a060000	// tell "Streamer" about delay factor
#define MCPL_SEND_PIXELS_BLOCK_GO_STREAMER		0x3a070000	// tell "streamer" to go!
//#define MCPL_SEND_PIXELS_BLOCK_DELAY	40
#define MCPL_SEND_PIXELS_BLOCK_DELAY	60

// The lower part of the key is the line that is expected to be sent. The node which has
// that line will then respond by sending the pixels.


/* 1. The root node will broadcast MCPL_BCAST_REPORT_HIST to all chips
 * 2. Each node then check if it is a leaf node
 *    A leaf node is a node which doesn't have any children.
 *    If it is a leaf node, then it send its histogram to the parent
 * 3. If it is not a leaf node, it will wait until its children send
 *    their histogram. Once it has received histogram from its children,
 *    it starts computing its own histogram and send the result to its
 *    parent.
 * 4. The process continues until it reaches the root node.
 * */
// mechanism for propagating histogram
#define MCPL_BCAST_REPORT_HIST		0xbca5000A	// broadcasted from chip<0,0,leadAp>
#define MCPL_BCAST_REPORT_HIST_MASK	0xFFFFFFFF	// it asks nodes to report in a tree mechanism
#define MCPL_REPORT_HIST2LEAD		0x1eaC0000	// each core report to leadAp using this key
#define MCPL_REPORT_HIST2LEAD_MASK	0xFFFF0000	//
#define MCPL_BCAST_HIST_RESULT		0xbcaC0000		// simply broadcast the histogram result
#define MCPL_BCAST_HIST_RESULT_MASK	0xFFFF0000	// where the key contains the seq, and the 
							// payload contains 4 pixels

/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/
/*-------------------------- Image Processing Stuffs --------------------------------*/
// Where will we put the frames
/* old version: we use fix address for image location
 * new version: it should be allocated via sark_xalloc
#define IMG_R_BUFF0_BASE		0x61000000	// for storing R-channel
#define IMG_G_BUFF0_BASE		0x62000000	// for storing G-channel
#define IMG_B_BUFF0_BASE		0x63000000	// for storing B-channel
#define IMG_O_BUFF1_BASE		0x64000000	// the result
#define IMG_O_BUFF2_BASE		0x65000000	// optional: result after filtering
#define IMG_O_BUFF3_BASE		0x66000000	// optional:
*/

#define IMG_GRAYSCALING				0
#define IMG_SOBEL					1
#define IMG_LAPLACE					2
#define IMG_DVS						3
#define IMG_WITHOUT_FILTER			0
#define IMG_WITH_FILTER				1


// Profiler message
#define PROF_MSG_PLL_INFO			1	// report about PLL
#define PROF_MSG_SET_FREQ			2	// set frequency
#define PROF_MSG_PROC_START			3	// inform that the processing is triggered
#define PROF_MSG_PROC_END			4	// inform that the processing is finish

#endif // DEFSPINNVID_H

