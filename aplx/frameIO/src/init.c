#include "frameIO.h"


/* See the previously working initRouter() in museum */
// TODO: we might move this initRouter() to the profiler to save ITCM
void initRouter()
{
	uint e, key, mask, dest, i;

	uint sdpRecv = 0x1FF << 8;
	uint pxFwdr = 0x1FF << 13;
	uint mcplRecv = 0x1FF << 18;
	uint streamer = 1 << 23;

	// key-1: send frame info to core 7-11 and 17 (streamer)
	e = rtr_alloc(1);
	key = MCPL_FRAMEIO_SZFRAME;
	mask = MCPL_FRAMEIO_MASK;
	//dest = 0x83E000;	// 1 00000 11111 00000 00 000000
	dest = pxFwdr | streamer;
	rtr_mc_set(e, key, mask, dest); e++;

	// key-2: sdpRecv cores tell pxFwdr where to fetch new gray pixels
	e = rtr_alloc(nCorePerPipe);
	for(i=0; i<nCorePerPipe; i++) {
		key = MCPL_FRAMEIO_NEWGRAY | (i + LEAD_CORE);
		dest = 1 << (i + 8 + nCorePerPipe);
		rtr_mc_set(e, key, mask, dest); e++;
	}

	// key-3: LEAD_CORE detect EOF, tell pxFwdr to normalize and/or fwd pixels
	// especially for core "7", it will continue with histogram chain
	e = rtr_alloc(1);
	key = MCPL_FRAMEIO_EOF_INT;
	dest = pxFwdr;
	rtr_mc_set(e, key, mask, dest); e++;

	// key-4: pxFwdr tells its next kin to fetch
	// always 1 core less than nCorePerPipe
	// eq: core 7 tells core 8, core 8 tells core 9, etc until core 11
	e = rtr_alloc(nCorePerPipe - 1);
	for(i=0; i<nCorePerPipe - 1; i++) {
		key = MCPL_FRAMEIO_HIST_CNTR_NEXT | (i + LEAD_CORE + nCorePerPipe);
		dest = 1 << (i + 8 + nCorePerPipe + 1);
		rtr_mc_set(e, key, mask, dest); e++;
	}

	// key-5: the last core in pxFwdr tells its group than histogram is ready
	e = rtr_alloc(1);
	key = MCPL_FRAMEIO_HIST_RDY;
	dest = pxFwdr;
	rtr_mc_set(e, key, mask, dest); e++;

	// key-6: pxFwdr tells its next kin that it has finished the bcasting
	// the last key responsible to broadcast EOF to external nodes
	e = rtr_alloc(nCorePerPipe);
	for(i=0; i<nCorePerPipe - 1; i++) {
		key = MCPL_FRAMEIO_EOF_EXT_RDY | (i + LEAD_CORE + nCorePerPipe);
		dest = 1 << (i + 8 + nCorePerPipe + 1);
		rtr_mc_set(e, key, mask, dest); e++;
	}
	key = MCPL_FRAMEIO_EOF_EXT;
	dest = 0x3F;	// to external links
	rtr_mc_set(e, key, mask, dest); e++;

	So, kapan broadcast pixel-nya? Lihat DEFSPINNVID_H






	uint inner = 0xFFFF80;			// excluding core-0 and external links
	uint profiler = 1 << (PROF_CORE+6);
	uint leader = 1 << (LEAD_CORE+6);
	uint workers = inner & ~profiler & ~streamer;	// for all workers in the chip
	uint x, y, d, c;

	/*----------------------------------------------------------------------------*/
	/*----------------------------------------------------------------------------*/
	/*----------------------- keys for intra-chip routing ------------------------*/
	// first, set individual destination, assuming all 15 worker cores are available
	e = rtr_alloc(15);
	if(e==0) {
		terminate_SpiNNVid(IO_STD, "initRouter err for intra-chip!\n", RTE_ABORT);
	} else {
		// ignore profiler and streamer
		for(uint i=LEAD_CORE; i<STREAMER_CORE; i++) {
			// starting from core-2 up to core-16
			rtr_mc_set(e, i, 0xFFFFFFFF, MC_CORE_ROUTE(i)); e++;
		}
	}
	// other broadcasting keys
	e = rtr_alloc(6);
	if(e==0)
	{
		terminate_SpiNNVid(IO_STD, "initRouter err for intra-chip!\n", RTE_ABORT);
	} else {
		rtr_mc_set(e, MCPL_BCAST_INFO_KEY,	0xFFFFFFFF, workers);	e++;
		rtr_mc_set(e, MCPL_PING_REPLY,		0xFFFFFFFF, leader);	e++;
		rtr_mc_set(e, MCPL_BCAST_GET_WLOAD, 0xFFFFFFFF, workers);	e++;
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
		rtr_mc_set(e, MCPL_BCAST_RESET_NET,		0xFFFFFFFF, dest); e++;
		rtr_mc_set(e, MCPL_BCAST_NET_DISCOVERY, 0xFFFFFFFF, dest); e++;
	}

	// special for MCPL_BLOCK_DONE_???
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
		rtr_mc_set(e, MCPL_BLOCK_DONE_TEDGE,	0xFFFF0000, dest); e++;
		rtr_mc_set(e, MCPL_BLOCK_DONE_TFILT,	0xFFFF0000, dest); e++;
		rtr_mc_set(e, MCPL_RECV_END_OF_FRAME,	0xFFFF0000, dest); e++;
		rtr_mc_set(e, MCPL_IGNORE_END_OF_FRAME, 0xFFFFFFFF, workers); e++;
		rtr_mc_set(e, MCPL_BCAST_NET_REPLY,		0xFFFFFFFF, dest); e++;
	}




	/*----------------------------------------------------------------------------*/
	/*----------------------------------------------------------------------------*/
	/*------------------------ keys for forwarding pixels ------------------------*/
	dest = workers;
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
	dest = workers;
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
	/*----------------- keys for sending result using buffering ------------------*/

	// This is the previous version, leadAp instructs workers to be ready...
	// key type-1: from leadAp-root to its own workers
	//if(blkInfo->nodeBlockID == 0) { // -> nodeBlockID is not available during this init()!!!!
	if(sv->p2p_addr == 0) {
		e = rtr_alloc(1);
		dest = workers;
		rtr_mc_set(e, MCPL_SEND_PIXELS_BLOCK_PREP, MCPL_SEND_PIXELS_BLOCK_MASK, dest); e++;
	}

	// key type-2: from the leadAp-root to other leadAps
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

	// key type-3: from the leadAp to workers
	e = rtr_alloc(1);
	dest = workers;
	rtr_mc_set(e, MCPL_SEND_PIXELS_BLOCK_CORES, MCPL_SEND_PIXELS_BLOCK_MASK, dest); e++;

	// key type-4: from worker to its leadAp
	e = rtr_alloc(1);
	dest = (1 << LEAD_CORE+6);	// equal to dest = leader;
	rtr_mc_set(e, MCPL_SEND_PIXELS_BLOCK_CORES_DONE, MCPL_SEND_PIXELS_BLOCK_MASK, dest); e++;

	// key type-5: from non-root-cores to root-cores
	e = rtr_alloc(30);	// 2 x 15 = 30
	for(c=LEAD_CORE; c<STREAMER_CORE; c++) {
		key = MCPL_SEND_PIXELS_BLOCK_CORES_DATA | (c << 16);
		// this is for sending toward core <0,0,leadAp>
		if (x>0 && y>0)			dest = (1 << 4);	// south-west
		else if(x>0 && y==0)	dest = (1 << 3);	// west
		else if(x==0 && y>0)	dest = (1 << 5);	// south
		else					dest = 1 << (6+c);
		rtr_mc_set(e, key, MCPL_SEND_PIXELS_BLOCK_MASK, dest); e++;
		// in addition to type-5, we also use "initial" message
		key = MCPL_SEND_PIXELS_BLOCK_CORES_INIT | (c << 16);
		rtr_mc_set(e, key, MCPL_SEND_PIXELS_BLOCK_MASK, dest); e++;
	}


	// key type-6: from root-cores to non-root-cores
	e = rtr_alloc(16);
	for(c=2; c<=17; c++) {
		dest = 1 << (6+c);
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
		key = MCPL_SEND_PIXELS_BLOCK_CORES_NEXT | (c << 16);
		rtr_mc_set(e, key, MCPL_SEND_PIXELS_BLOCK_MASK, dest); e++;
	}

	// key type-7: from node-leadAp to root-leadAp
	e = rtr_alloc(1);
	// this is for sending toward core <0,0,leadAp>
	if (x>0 && y>0)			dest = (1 << 4);	// south-west
	else if(x>0 && y==0)	dest = (1 << 3);	// west
	else if(x==0 && y>0)	dest = (1 << 5);	// south
	else					dest = leader;
	rtr_mc_set(e, MCPL_SEND_PIXELS_BLOCK_DONE, MCPL_SEND_PIXELS_BLOCK_MASK, dest); e++;

	// key type-8: tell the streamer to start streamout
	if(sv->p2p_addr==0) {
		e = rtr_alloc(3);
		dest = 1 << (STREAMER_CORE + 6);
		rtr_mc_set(e, MCPL_SEND_PIXELS_BLOCK_GO_STREAMER, MCPL_SEND_PIXELS_BLOCK_MASK, dest); e++;
		rtr_mc_set(e, MCPL_SEND_PIXELS_BLOCK_INFO_STREAMER_SZIMG, MCPL_SEND_PIXELS_BLOCK_MASK, dest); e++;
		rtr_mc_set(e, MCPL_SEND_PIXELS_BLOCK_INFO_STREAMER_DEL, MCPL_SEND_PIXELS_BLOCK_MASK, dest); e++;
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
	// prepare the result data
	resultMsg.flags = 0x07;
	resultMsg.tag = SDP_TAG_RESULT;
	resultMsg.dest_port = PORT_ETH;
	resultMsg.dest_addr = sv->eth_addr;
	//resultMsg.length = ??? --> need to be modified later

	// and the histogram data
	histMsg.flags = 0x07;
	histMsg.tag = 0;
	histMsg.srce_addr = sv->p2p_addr;
	histMsg.srce_port = (SDP_PORT_HISTO << 5) + myCoreID;
	histMsg.dest_port = (SDP_PORT_HISTO << 5) + 1;	// WARNING: assuming leadAp is core-1
	histMsg.length = sizeof(sdp_hdr_t) + sizeof(cmd_hdr_t) + 256;
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

