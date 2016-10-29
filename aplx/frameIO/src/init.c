#include "frameIO.h"

// TODO: how can we detect how many cores are running frameIO?
// solution: load frameIO.aplx on cores 3-17 first, then on core 2
// then LEAD_CORE (core 2) will be able to count properly
void getNumCorePerPipe(uint *nC, uint *sC)
{
	// at the moment, let's just keep this:
	*nC = 5;
	*sC = 17;
}

// NOTE: SpiNNVid has its own initRouter!
void initRouter()
{
	uint e, key, mask, dest, i;

	uint extLink = 0x3F;
	uint profiler = 1 << (6 + PROF_CORE);
	uint streamer = 1 << (nCorePerPipe + streamerCore);

	uint sdpRecv, pxFwdr, mcplRecv;

	uchar sdpRecvStart = 8;
	uchar pxFwdrStart = 8+nCorePerPipe;
	uchar mcplRecvStart = 8+2*nCorePerPipe;

	// How many cores are assigned for sdpRecv, pxFwdr, and mcplRecv?
	e = 0;
	for(i=0; i<nCorePerPipe; i++)
		e |= (1 << i);

	sdpRecv = e << sdpRecvStart;
	pxFwdr = 0x1FF << pxFwdrStart;
	mcplRecv = 0x1FF << mcplRecvStart;

	// default mask
	mask = MCPL_FRAMEIO_MASK;
	//dest :              ST mcplR fwd   sdpR     extL
	//dest = 0x83E000;	// 1 00000 11111 00000 00 000000

	// key-1: LEAD_CORE inform pxFwdr about their wID
	e = rtr_alloc(nCorePerPipe);
	for(i=0; i<nCorePerPipe; i++) {
		key = MCPL_FRAMEIO_FWD_WID | i;
		dest = 1 << (i + pxFwdrStart);
		rtr_mc_set(e, key, mask, dest); e++;
	}



	// key-2: send sdram image buffer address to pxFwdr, mcplRecv, and streamer
	e = rtr_alloc(1);
	key = MCPL_FRAMEIO_SDRAM_BUF_ADDR;
	// both mcplRecv and pxFwdr may receive this key, but they might ignore it
	dest = streamer | mcplRecv | pxFwdr;
	rtr_mc_set(e, key, mask, dest);



	// key-2: send frame info to core 7-11, 17 (streamer),
	// and extern (SpiNNVid needs to compute workload)
	e = rtr_alloc(1);
	key = MCPL_FRAMEIO_SZFRAME;
	dest = pxFwdr | streamer | extLink;
	rtr_mc_set(e, key, mask, dest); e++;
	// NOTE: number of nCorePerPipe must be included in the key_hdr
	// receiving this, then pxFwdr cores compute its workload



	// key-3: sdpRecv cores tell pxFwdr where and how much new gray pixels from SYSRAM
	e = rtr_alloc(2*nCorePerPipe);
	for(i=0; i<nCorePerPipe; i++) {
		dest = 1 << (i + pxFwdrStart);
		key = MCPL_FRAMEIO_SYSRAM_BUF_ADDR | (i + LEAD_CORE);
		rtr_mc_set(e, key, mask, dest); e++;
		key = MCPL_FRAMEIO_NEWGRAY | (i + LEAD_CORE);
		rtr_mc_set(e, key, mask, dest); e++;
	}




	// key-4: LEAD_CORE detect EOF, tell all pxFwdr cores to normalize and/or fwd pixels
	// especially for core "7", it will continue with histogram chain
	e = rtr_alloc(1);
	key = MCPL_FRAMEIO_EOF_INT;
	dest = pxFwdr;
	rtr_mc_set(e, key, mask, dest); e++;




	// key-5: pxFwdr tells its next kin to fetch
	// always 1 core less than nCorePerPipe
	// eq: core 7 tells core 8, core 8 tells core 9, etc until core 11
	e = rtr_alloc(nCorePerPipe - 1);
	for(i=0; i<nCorePerPipe - 1; i++) {
		key = MCPL_FRAMEIO_HIST_CNTR_NEXT | (i + LEAD_CORE + nCorePerPipe);
		dest = 1 << (i + pxFwdrStart + 1);
		rtr_mc_set(e, key, mask, dest); e++;
	}




	// key-6: the last core in pxFwdr tells its group than histogram is ready
	e = rtr_alloc(1);
	key = MCPL_FRAMEIO_HIST_RDY;
	dest = pxFwdr;
	rtr_mc_set(e, key, mask, dest); e++;
	// then each core fetch and compute the hitogram normalizer
	// each core fetch image data from sdram, apply the histogram and
	// broadcast it
	// NOTE: no need for storing the normalized image here, let SpiNNVid do it




	// key-7: pxFwdr tells its next kin that it has finished the bcasting
	// the last key responsible to broadcast EOF to external nodes
	e = rtr_alloc(nCorePerPipe);
	for(i=0; i<nCorePerPipe - 1; i++) {
		key = MCPL_FRAMEIO_EOF_EXT_RDY | (i + LEAD_CORE + nCorePerPipe);
		dest = 1 << (i + pxFwdrStart + 1);
		rtr_mc_set(e, key, mask, dest); e++;
	}
	key = MCPL_FRAMEIO_EOF_EXT;
	dest = 0x3F;	// to external links
	rtr_mc_set(e, key, mask, dest); e++;





	// key-8: tell profiler, pxFwdr, streamer and extLink about op_info
	// pxFwdr needs it to determine if histogram is required
	// streamer needs it to compute sdpDelayFactorSpin
	e = rtr_alloc(1);
	key = MCPL_FRAMEIO_OP_INFO;
	dest = profiler | pxFwdr | streamer | extLink;
	rtr_mc_set(e, key, mask, dest);


	// key for MCPL_BCAST_ALL_REPORT that goes everywhere
	e = rtr_alloc(1);
	key = MCPL_BCAST_ALL_REPORT;
	dest = profiler | sdpRecv | pxFwdr | mcplRecv | streamer | extLink;
	rtr_mc_set(e, key, mask, dest);

	// key for MCPL_BCAST_RESET_NET that goes to SpiNNVid
	e = rtr_alloc(1);
	key = MCPL_BCAST_RESET_NET;
	dest = extLink;
	rtr_mc_set(e, key, mask, dest);

	// So, kapan broadcast pixel-nya? Lihat DEFSPINNVID_H
}





void initSDP()
{
	// prepare the result data
	resultMsg.flags = 0x07;
	resultMsg.tag = SDP_TAG_RESULT;
	resultMsg.dest_port = PORT_ETH;
	resultMsg.dest_addr = sv->eth_addr;
	//resultMsg.length = ??? --> need to be modified later

}

// TODO: iptags aren't set properly for different IP address
void initIPTag()
{
	// only chip <0,0>
	if(sv->p2p_addr==0) {
		// prepare the sdp packet
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

