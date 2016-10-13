#include "SpiNNVid.h"

// the sanity check:
// check if the board and app-id are correct
void initCheck()
{
	uchar ip = sv->ip_addr[3];

	// only root-node has ip variable correctly specified !!!
	if(sv->p2p_addr==0) {

// note on USING_SPIN: it is defined in the Makefile.3 and Makefile.5
#if(USING_SPIN==3)
		if(ip!=SPIN3_END_IP) {
			io_printf(IO_STD, "Invalid target board! Got IP-%d\n", ip);
			// rt_error(RTE_ABORT);	// cannot use rt_error and spin1_exit together!!!
			// spin1_exit(RTE_ABORT);	// see ~/Projects/SpiNN/general/miscTest/cekTimer
			terminate_SpiNNVid(IO_DBG, NULL, RTE_SWERR);
		}
#else
		if(ip != SPIN5_END_IP) {
			io_printf(IO_STD, "Invalid target board! Got IP-%d\n", ip);
			terminate_SpiNNVid(IO_DBG, NULL, RTE_SWERR);
		}
#endif
	}

	// check App-ID
	if(sark_app_id() != SPINNVID_APP_ID) {
		io_printf(IO_STD, "Invalid App-ID! Please assign this ID: %d\n", SPINNVID_APP_ID);
		terminate_SpiNNVid(IO_DBG, NULL, RTE_SWERR);
	}

}


inline void initImgBufs()
{
	blkInfo->imgRIn = NULL;
	blkInfo->imgGIn = NULL;
	blkInfo->imgBIn = NULL;
	blkInfo->imgOut1 = NULL;
	//blkInfo->imgOut2 = NULL;
	//blkInfo->imgOut3 = NULL;
}

void releaseImgBuf()
{
	if(blkInfo->imgRIn != NULL) {
#if(DEBUG_LEVEL>1)
		if(sv->p2p_addr==0)
			io_printf(IO_STD, "[IMGBUF] Releasing SDRAM heap...\n");
		else
			io_printf(IO_BUF, "[IMGBUF] Releasing SDRAM heap...\n");
#endif
		sark_xfree(sv->sdram_heap, blkInfo->imgRIn, ALLOC_LOCK);
		sark_xfree(sv->sdram_heap, blkInfo->imgGIn, ALLOC_LOCK);
		sark_xfree(sv->sdram_heap, blkInfo->imgBIn, ALLOC_LOCK);
		sark_xfree(sv->sdram_heap, blkInfo->imgOut1, ALLOC_LOCK);
		//sark_xfree(sv->sdram_heap, blkInfo->imgOut2, ALLOC_LOCK);
		//sark_xfree(sv->sdram_heap, blkInfo->imgOut3, ALLOC_LOCK);
	}
}

// allocatedImgBuf() is for allocating imgRIn etc
// it should be called with SDP_CMD_FRAME_INFO and MCPL_BCAST_FRAME_INFO
void allocateImgBuf()
{
#if(DEBUG_LEVEL>1)
	if(sv->p2p_addr==0)
		io_printf(IO_STD, "[IMGBUF] Allocating SDRAM heap...\n");
	else
		io_printf(IO_BUF, "[IMGBUF] Allocating SDRAM heap...\n");
#endif

	// at this point, we just need to know the size of the image
	uint sz = blkInfo->wImg * blkInfo->hImg;

	// image buffers have been allocated already? if yes, clear them first
	releaseImgBuf();

	/* debugging, are the buffers already allocated...
	char *stream;
	if(sv->p2p_addr==0) stream=IO_STD; else stream=IO_BUF;
	io_printf(stream, "imgRIn = 0x%x\n", blkInfo->imgRIn);
	io_printf(stream, "imgGIn = 0x%x\n", blkInfo->imgGIn);
	io_printf(stream, "imgBIn = 0x%x\n", blkInfo->imgBIn);
	io_printf(stream, "imgOut1 = 0x%x\n", blkInfo->imgOut1);
	*/

	blkInfo->imgRIn = sark_xalloc(sv->sdram_heap, sz, XALLOC_TAG_IMGRIN, ALLOC_LOCK);
	if(blkInfo->imgRIn==NULL)
		terminate_SpiNNVid(IO_DBG, "[FATAL] Cannot allocate imgRIn\n", RTE_ABORT);
	blkInfo->imgGIn = sark_xalloc(sv->sdram_heap, sz, XALLOC_TAG_IMGGIN, ALLOC_LOCK);
	if(blkInfo->imgGIn==NULL)
		terminate_SpiNNVid(IO_DBG, "[FATAL] Cannot allocate imgGIn\n", RTE_ABORT);
	blkInfo->imgBIn = sark_xalloc(sv->sdram_heap, sz, XALLOC_TAG_IMGBIN, ALLOC_LOCK);
	if(blkInfo->imgBIn==NULL)
		terminate_SpiNNVid(IO_DBG, "[FATAL] Cannot allocate imgBIn\n", RTE_ABORT);
	blkInfo->imgOut1 = sark_xalloc(sv->sdram_heap, sz, XALLOC_TAG_IMGOUT1, ALLOC_LOCK);
	if(blkInfo->imgOut1==NULL)
		terminate_SpiNNVid(IO_DBG, "[FATAL] Cannot allocate imgOut1\n", RTE_ABORT);
	/*
	blkInfo->imgOut2 = sark_xalloc(sv->sdram_heap, sz, XALLOC_TAG_IMGOUT2, ALLOC_LOCK);
	if(blkInfo->imgOut2==NULL)
		terminate_SpiNNVid(IO_DBG, "[FATAL] Cannot allocate imgOut2\n", RTE_ABORT);
	blkInfo->imgOut3 = sark_xalloc(sv->sdram_heap, sz, XALLOC_TAG_IMGOUT3, ALLOC_LOCK);
	if(blkInfo->imgOut3==NULL)
		terminate_SpiNNVid(IO_DBG, "[FATAL] Cannot allocate imgOut3\n", RTE_ABORT);
	*/
	// Remember: clean these if video ends!!!

}






/* initRouter() initialize MCPL routing table by leadCore. Normally, there are two keys:
 * MCPL_BCAST_KEY and MCPL_TO_LEADER
 * */
void initRouter()
{
	uint allRoute = 0xFFFF80;			// excluding core-0 and external links
	uint profiler = (1 << (PROF_CORE+6));
	uint leader = (1 << (myCoreID+6));	// only for leadAp
	uint workers = allRoute & ~leader;	// for other workers in the chip
	workers &= (~profiler);				// exclude profiler
	allRoute &= (~profiler);				// exclude profiler
	uint key, dest;
	uint x, y, d;




	/*----------------------------------------------------------------------------*/
	/*----------------------------------------------------------------------------*/
	/*----------------------- keys for intra-chip routing ------------------------*/
	// first, set individual destination, assuming all 17-cores are available
	uint e = rtr_alloc(17);
	if(e==0) {
		terminate_SpiNNVid(IO_STD, "initRouter err for intra-chip!\n", RTE_ABORT);
	} else {
		// each destination core might have its own key association
		// so that leadAp can tell each worker, which region is their part
		for(uint i=0; i<17; i++)
			// starting from core-1 up to core-17
			rtr_mc_set(e+i, i+1, 0xFFFFFFFF, (MC_CORE_ROUTE(i+1)));
	}
	// other broadcasting keys
	e = rtr_alloc(6);
	if(e==0)
	{
		terminate_SpiNNVid(IO_STD, "initRouter err for intra-chip!\n", RTE_ABORT);
	} else {
		rtr_mc_set(e, MCPL_BCAST_INFO_KEY,	0xFFFFFFFF, workers);	e++;
		rtr_mc_set(e, MCPL_PING_REPLY,		0xFFFFFFFF, leader);	e++;
		rtr_mc_set(e, MCPL_BCAST_GET_WLOAD, 0xFFFFFFFF, allRoute);	e++;
		rtr_mc_set(e, MCPL_EDGE_DONE,		0xFFFFFFFF, leader);	e++;
		rtr_mc_set(e, MCPL_FILT_DONE,		0xFFFFFFFF, leader);	e++;
		rtr_mc_set(e, MCPL_REPORT_HIST2LEAD, MCPL_REPORT_HIST2LEAD_MASK, leader); e++;
	}




	/*----------------------------------------------------------------------------*/
	/*----------------------------------------------------------------------------*/
	/*----------------------- keys for inter-chip routing ------------------------*/
	x = CHIP_X(sv->p2p_addr);
	y = CHIP_Y(sv->p2p_addr);

	// broadcasting from root node like:
	// MCPL_BCAST_SEND_RESULT, MCPL_BCAST_RESET_NET, etc
	dest = leader;	// by default, go to leadAP, unless:
#if(USING_SPIN==5)
	if(x==y) {
		if(x==0)
			dest = 1 + (1 << 1) + (1 << 2);
		else if(x<7)
			dest += 1 + (1 << 1) + (1 << 2);
	}
	else if(x>y) {
		d = x - y;
		if(x<7 && d<4)
			dest += 1;
	}
	else if(x<y) {
		d = y - x;
		if(d<3 && y<7)
			dest += (1 << 2);
	}
#elif(USING_SPIN==3)
	if(sv->p2p_addr==0)
		dest = 1 + (1<<1) + (1<<2);
#endif
	e = rtr_alloc(6);
	if(e==0)
	{
		terminate_SpiNNVid(IO_STD, "initRouter err for inter-chip!\n", RTE_ABORT);
	} else {
		rtr_mc_set(e, MCPL_BCAST_NODES_INFO,	0xFFFFFFFF, dest); e++;
		rtr_mc_set(e, MCPL_BCAST_OP_INFO,		0xFFFFFFFF, dest); e++;
		rtr_mc_set(e, MCPL_BCAST_FRAME_INFO,	0xFFFFFFFF, dest); e++;
		rtr_mc_set(e, MCPL_BCAST_SEND_RESULT,	0xFFFFFFFF, dest); e++;
		rtr_mc_set(e, MCPL_BCAST_RESET_NET,		0xFFFFFFFF, dest); e++;
		rtr_mc_set(e, MCPL_BCAST_NET_DISCOVERY, 0xFFFFFFFF, dest); e++;
	}

	// special for MCPL_BLOCK_DONE
	// this is for sending toward core <0,0,leadAp>
	if (x>0 && y>0)			dest = (1 << 4);	// south-west
	else if(x>0 && y==0)	dest = (1 << 3);	// west
	else if(x==0 && y>0)	dest = (1 << 5);	// south
	else					dest = leader;
	e = rtr_alloc(5);
	if(e==0)
	{
		terminate_SpiNNVid(IO_STD, "initRouter err for special keys!\n", RTE_ABORT);
	} else {
		//rtr_mc_set(e, MCPL_BLOCK_DONE, 0xFFFFFFFF, dest); e++;
		rtr_mc_set(e, MCPL_BLOCK_DONE_TEDGE,	0xFFFF0000, dest); e++;
		rtr_mc_set(e, MCPL_BLOCK_DONE_TFILT,	0xFFFF0000, dest); e++;
		rtr_mc_set(e, MCPL_RECV_END_OF_FRAME,	0xFFFF0000, dest); e++;
		rtr_mc_set(e, MCPL_IGNORE_END_OF_FRAME, 0xFFFFFFFF, workers); e++;
		rtr_mc_set(e, MCPL_BCAST_NET_REPLY,		0xFFFFFFFF, dest); e++;
	}




	/*----------------------------------------------------------------------------*/
	/*----------------------------------------------------------------------------*/
	/*------------------------ keys for forwarding pixels ------------------------*/
	dest = allRoute;
#if(USING_SPIN==5)
	if(x==y) {
		if(x<7)
			dest += 1 + (1 << 1) + (1 << 2);
	}
	else if(x>y) {
		d = x - y;
		if(x<7 && d<4)
			dest += 1;
	}
	else if(x<y) {
		d = y - x;
		if(d<3 && y<7)
			dest += (1 << 2);
	}
#elif(USING_SPIN==3)
	if(sv->p2p_addr==0)
		dest = 1 + (1<<1) + (1<<2);
#endif
	e = rtr_alloc(6);
	if(e==0)
	{
		terminate_SpiNNVid(IO_STD, "initRouter err for pixel forwarding!\n", RTE_ABORT);
	} else {
		rtr_mc_set(e, MCPL_FWD_PIXEL_INFO,	MCPL_FWD_PIXEL_MASK, dest); e++;
		rtr_mc_set(e, MCPL_FWD_PIXEL_RDATA, MCPL_FWD_PIXEL_MASK, dest); e++;
		rtr_mc_set(e, MCPL_FWD_PIXEL_GDATA, MCPL_FWD_PIXEL_MASK, dest); e++;
		rtr_mc_set(e, MCPL_FWD_PIXEL_BDATA, MCPL_FWD_PIXEL_MASK, dest); e++;
		rtr_mc_set(e, MCPL_FWD_PIXEL_YDATA, MCPL_FWD_PIXEL_MASK, dest); e++;
		rtr_mc_set(e, MCPL_FWD_PIXEL_EOF,	MCPL_FWD_PIXEL_MASK, dest); e++;
	}




	/*----------------------------------------------------------------------------*/
	/*----------------------------------------------------------------------------*/
	/*------------------------- keys for all routing -----------------------------*/
	dest = allRoute;
#if(USING_SPIN==5)
	if(x==y) {
		if(x<7)
			dest += 1 + (1 << 1) + (1 << 2);
	}
	else if(x>y) {
		d = x - y;
		if(x<7 && d<4)
			dest += 1;
	}
	else if(x<y) {
		d = y - x;
		if(d<3 && y<7)
			dest += (1 << 2);
	}
#elif(USING_SPIN==3)
	if(sv->p2p_addr==0)
		dest += 1 + (1<<1) + (1<<2);
#endif
	e = rtr_alloc(4);
	if(e==0)
	{
		terminate_SpiNNVid(IO_STD, "initRouter err for all-routing!\n", RTE_ABORT);
	} else {
		rtr_mc_set(e, MCPL_BCAST_ALL_REPORT, 0xFFFFFFFF, dest); e++;
		rtr_mc_set(e, MCPL_BCAST_START_PROC, 0xFFFFFFFF, dest); e++;
		// regarding histogram equalization
		rtr_mc_set(e, MCPL_BCAST_REPORT_HIST, MCPL_BCAST_REPORT_HIST_MASK, dest); e++;
		rtr_mc_set(e, MCPL_BCAST_HIST_RESULT, MCPL_BCAST_HIST_RESULT_MASK, dest); e++;
	}




	/*----------------------------------------------------------------------------*/
	/*----------------------------------------------------------------------------*/
	/*------------------------ keys for sending result ---------------------------*/
	// key type-1: from the root to other nodes
	e = rtr_alloc(5);
	dest = leader;
#if(USING_SPIN==5)
	if(x==y) {
		if(x==0)
			dest = 1 + (1 << 1) + (1 << 2);
		else if(x<7)
			dest += 1 + (1 << 1) + (1 << 2);
	}
	else if(x>y) {
		d = x - y;
		if(x<7 && d<4)
			dest += 1;
	}
	else if(x<y) {
		d = y - x;
		if(d<3 && y<7)
			dest += (1 << 2);
	}
#elif(USING_SPIN==3)
	if(sv->p2p_addr==0)
		dest = 1 + (1<<1) + (1<<2);
#endif
	rtr_mc_set(e, MCPL_SEND_PIXELS_CMD, MCPL_SEND_PIXELS_MASK, dest); e++;
	rtr_mc_set(e, MCPL_SEND_PIXELS_NEXT, MCPL_SEND_PIXELS_MASK, dest); e++;

	// key type-2: from other nodes to the root
	if (x>0 && y>0)			dest = (1 << 4);	// south-west
	else if(x>0 && y==0)	dest = (1 << 3);	// west
	else if(x==0 && y>0)	dest = (1 << 5);	// south
	else					dest = leader;
	rtr_mc_set(e, MCPL_SEND_PIXELS_DATA, MCPL_SEND_PIXELS_MASK, dest); e++;
	rtr_mc_set(e, MCPL_SEND_PIXELS_INFO, MCPL_SEND_PIXELS_MASK, dest); e++;
	rtr_mc_set(e, MCPL_SEND_PIXELS_DONE, MCPL_SEND_PIXELS_MASK, dest); e++;



	/*----------------------------------------------------------------------------*/
	/*----------------------------------------------------------------------------*/
	/*----------------- keys for sending result using buffering ------------------*/
	// key type-1: from the leadAp-root to other leadAps
	e = rtr_alloc(1);
	dest = leader;
#if(USING_SPIN==5)
	if(x==y) {
		if(x==0)
			dest = 1 + (1 << 1) + (1 << 2);
		else if(x<7)
			dest += 1 + (1 << 1) + (1 << 2);
	}
	else if(x>y) {
		d = x - y;
		if(x<7 && d<4)
			dest += 1;
	}
	else if(x<y) {
		d = y - x;
		if(d<3 && y<7)
			dest += (1 << 2);
	}
#elif(USING_SPIN==3)
	if(sv->p2p_addr==0)
		dest = 1 + (1<<1) + (1<<2);
#endif
	rtr_mc_set(e, MCPL_SEND_PIXELS_BLOCK, MCPL_SEND_PIXELS_BLOCK_MASK, dest); e++;

	// key type-2: from the leadAp to workers
	e = rtr_alloc(1);
	dest = allRoute;
	rtr_mc_set(e, MCPL_SEND_PIXELS_BLOCK_CORES, MCPL_SEND_PIXELS_BLOCK_MASK, dest); e++;

	// key type-3: from non-root-cores to root-cores
	e = rtr_alloc(16);
	for(d=2; d<=17; d++) {
		key = MCPL_SEND_PIXELS_BLOCK_CORES_DATA | (d << 16);
	}






	/*----------------------------------------------------------------------------*/
	/*----------------------------------------------------------------------------*/
	/*----------------------- other keys with special routing --------------------*/

	// communication with the profiler:
	// 1. all profilers:
	// 2. internal profiler:
	dest = profiler;
#if(USING_SPIN==5)
	if(x==y) {
		if(x<7)
			dest += 1 + (1 << 1) + (1 << 2);
	}
	else if(x>y) {
		d = x - y;
		if(x<7 && d<4)
			dest += 1;
	}
	else if(x<y) {
		d = y - x;
		if(d<3 && y<7)
			dest += (1 << 2);
	}
#elif(USING_SPIN==3)
	if(sv->p2p_addr==0)
		dest += 1 + (1 << 1) + (1 << 2);
#endif
	e = rtr_alloc(2);
	if(e==0)
	{
		terminate_SpiNNVid(IO_STD, "initRouter err for other keys!\n", RTE_ABORT);
	} else {
		rtr_mc_set(e, MCPL_TO_ALL_PROFILER,	0xFFFFFFFF, dest); e++;
		rtr_mc_set(e, MCPL_TO_OWN_PROFILER, 0xFFFFFFFF, profiler); e++;
	}
}

void initSDP()
{
	// prepare the reply message
	replyMsg.flags = 0x07;	//no reply
	replyMsg.tag = SDP_TAG_REPLY;
	//replyMsg.srce_port = (SDP_PORT_CONFIG << 5) + myCoreID;
	replyMsg.srce_port = (SDP_PORT_B_IMG_DATA << 5) + myCoreID;
	replyMsg.srce_addr = sv->p2p_addr;
	replyMsg.dest_port = PORT_ETH;
	replyMsg.dest_addr = sv->eth_addr;
	replyMsg.length = sizeof(sdp_hdr_t);	// it's fix!

	// prepare the result data
	resultMsg.flags = 0x07;
	// what if:
	// - srce_addr contains image line number + rgb info
	// - srce_port contains the data sequence
	//resultMsg.srce_port = 		// during sending, this must be modified
	//resultMsg.srce_addr =
#if (DESTINATION==DEST_HOST)
	resultMsg.tag = SDP_TAG_RESULT;
	resultMsg.dest_port = PORT_ETH;
	resultMsg.dest_addr = sv->eth_addr;
#elif (DESTINATION==DEST_FPGA)
	resultMsg.tag = 0;
	resultMsg.dest_port = (SDP_PORT_FPGA_OUT << 5) + LEAD_CORE;
	resultMsg.dest_addr = 0;
#endif
	//resultMsg.length = ??? --> need to be modified later

	// and the debug data
	debugMsg.flags = 0x07;
	debugMsg.tag = SDP_TAG_DEBUG;
	debugMsg.srce_port = (SDP_PORT_CONFIG << 5) + myCoreID;
	debugMsg.srce_addr = sv->p2p_addr;
	debugMsg.dest_port = PORT_ETH;
	debugMsg.dest_addr = sv->eth_addr;
	debugMsg.length = sizeof(sdp_hdr_t) + sizeof(cmd_hdr_t);

	// and the histogram data
	histMsg.flags = 0x07;
	histMsg.tag = 0;
	histMsg.srce_addr = sv->p2p_addr;
	histMsg.srce_port = (SDP_PORT_HISTO << 5) + myCoreID;
	histMsg.dest_port = (SDP_PORT_HISTO << 5) + 1;	// WARNING: assuming leadAp is core-1
	histMsg.length = sizeof(sdp_hdr_t) + sizeof(cmd_hdr_t) + 256;
}

void initOther()
{
	pixelCntr = 0;  // for many purpose, including forwarding using MCPL

	// just to make sure that forwarded packet buffer are empty/ready
	for(uchar ch=0; ch<3; ch++)
		fwdPktBuffer[ch].pxLen = 0;
}

// TODO: iptags aren't set properly for different IP address
void initIPTag()
{
	// only chip <0,0>
	if(sv->p2p_addr==0) {
		sdp_msg_t iptag;
		iptag.flags = 0x07;	// no replay
		iptag.tag = 0;		// internal
		iptag.srce_addr = 0;
		iptag.srce_port = 0xE0 + myCoreID;	// use port-7
		iptag.dest_addr = 0;
		iptag.dest_port = 0;				// send to the "monitor core"
		iptag.cmd_rc = 26;
		// set the reply tag
		iptag.arg1 = (1 << 16) + SDP_TAG_REPLY;
		iptag.arg2 = SDP_UDP_REPLY_PORT;
		iptag.arg3 = SDP_HOST_IP;
		iptag.length = sizeof(sdp_hdr_t) + sizeof(cmd_hdr_t);
		spin1_send_sdp_msg(&iptag, 10);
		// set the result tag
		iptag.arg1 = (1 << 16) + SDP_TAG_RESULT;
		iptag.arg2 = SDP_UDP_RESULT_PORT;
		spin1_send_sdp_msg(&iptag, 10);
		// set the debug tag
		iptag.arg1 = (1 << 16) + SDP_TAG_DEBUG;
		iptag.arg2 = SDP_UDP_DEBUG_PORT;
		spin1_send_sdp_msg(&iptag, 10);
		// set the profiler tag
		iptag.arg1 = (1 << 16) + SDP_TAG_PROFILER;
		iptag.arg2 = SDP_UDP_PROFILER_PORT;
		spin1_send_sdp_msg(&iptag, 10);
	}
}

// initHistData() should be called whenever a new image arrives
// i.e., during the computeWLoad()
void initHistData(uint arg0, uint arg1)
{
	for(uchar i=0; i<256; i++) {
		hist[i] = 0;
		child1hist[i] = 0;
		child2hist[i] = 0;
	}
	// construct histPropTree, so we know where to send the histogram result
	// parent = (myNodeID - 1) / 2
	// child_1 = (myNodeID * 2) + 1
	// child_2 = child_1 + 1
	if(blkInfo->nodeBlockID == 0)
		histPropTree.p = -1;	// no parent
	else
		histPropTree.p = (blkInfo->nodeBlockID - 1) / 2;
	short c, nc;
	histPropTree.c[0] = -1;		// no childer, by default
	histPropTree.c[1] = -1;
	c = blkInfo->nodeBlockID * 2 +1;
	if(c < blkInfo->maxBlock) {
		histPropTree.c[0] = c;
		nc = 1;
		if((c + 1) < blkInfo->maxBlock) {
			histPropTree.c[1] = c + 1;
			nc++;
		}
		histPropTree.isLeaf = 0;
	}
	else {
		nc = 0;
		histPropTree.isLeaf = 1;	// I am a leaf node?
	}

	// prepare histMsg header
	// TODO: how can the node know the address of its parent?
	ushort px, py;
	getChipXYfromID(histPropTree.p, &px, &py);
	histMsg.dest_addr = (px << 8) + py;
	histMsg.cmd_rc = SDP_CMD_REPORT_HIST;
	histMsg.seq = blkInfo->nodeBlockID;

	// additionally for leadAp:
	// it receives MCPL_REPORT_HIST2LEAD from other cores this many:
	histPropTree.maxHistMCPLItem = (nCoresForPixelPreProc-1) * 256;
	histPropTree.MCPLItemCntr = 0;
	histPropTree.maxHistSDPItem = nc*4;
	histPropTree.SDPItemCntr = 0;
}

