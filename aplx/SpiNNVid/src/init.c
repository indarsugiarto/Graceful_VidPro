#include "SpiNNVid.h"

// the sanity check:
// 1. check if the board is correct --> we use fix N-nodes
void initCheck()
{
	uchar ip = sv->ip_addr[3];
	if(sv->p2p_addr==0) {
#if(USING_SPIN==3)
		if(ip!=253) {
			io_printf(IO_STD, "Invalid target board!\n");
			rt_error(RTE_ABORT);
			spin1_exit(1);
		}
#else
		if(ip != 1) {
			io_printf(IO_STD, "Invalid target board!\n");
			rt_error(RTE_ABORT);
			spin1_exit(1);
		}
#endif
	}

	// check App-ID
	if(sark_app_id() != SPINNVID_APP_ID) {
		io_printf(IO_STD, "Invalid App-ID!\n");
		rt_error(RTE_ABORT);
		spin1_exit(1);
	}
}

/* initRouter() initialize MCPL routing table by leadAp. Normally, there are two keys:
 * MCPL_BCAST_KEY and MCPL_TO_LEADER
 * */
void initRouter()
{
	uint allRoute = 0xFFFF80;			// excluding core-0 and external links
	uint leader = (1 << (myCoreID+6));	// only for leadAp
	uint workers = allRoute & ~leader;	// for other workers in the chip
	uint dest;
	ushort x, y, d;

	/*-----------------------------------------------*/
	/*--------- keys for intra-chip routing ---------*/
	// first, set individual destination, assuming all 17-cores are available
	uint e = rtr_alloc(17);
	if(e==0) {
		io_printf(IO_STD, "initRouter err!\n");
		rt_error(RTE_ABORT);
	} else {
		// each destination core might have its own key association
		// so that leadAp can tell each worker, which region is their part
		for(uint i=0; i<17; i++)
			// starting from core-1 up to core-17
			rtr_mc_set(e+i, i+1, 0xFFFFFFFF, (MC_CORE_ROUTE(i+1)));
	}
	// other broadcasting keys
	e = rtr_alloc(2);
	if(e==0)
	{
		io_printf(IO_STD, "initRouter err!\n");
		rt_error(RTE_ABORT);
	} else {
		rtr_mc_set(e, MCPL_BCAST_INFO_KEY, 0xFFFFFFFF, workers); e++;
		rtr_mc_set(e, MCPL_PING_REPLY, 0xFFFFFFFF, leader); e++;
	}

	/*-----------------------------------------------*/
	/*--------- keys for inter-chip routing ---------*/
	x = CHIP_X(sv->p2p_addr);
	y = CHIP_Y(sv->p2p_addr);

	// broadcasting from root node like:
	// MCPL_BCAST_SEND_RESULT, MCPL_BCAST_*pixels and HOST_ACK
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
	e = rtr_alloc(2);
	if(e==0)
	{
		io_printf(IO_STD, "initRouter err!\n");
		rt_error(RTE_ABORT);
	} else {
		rtr_mc_set(e, MCPL_BCAST_NODES_INFO, 0xFFFFFFFF, dest); e++;
		rtr_mc_set(e, MCPL_BCAST_OP_INFO, 0xFFFFFFFF, dest); e++;
	}
}

void initSDP()
{
	// prepare the reply message
	replyMsg.flags = 0x07;	//no reply
	replyMsg.tag = SDP_TAG_REPLY;
	replyMsg.srce_port = (SDP_PORT_CONFIG << 5) + myCoreID;
	replyMsg.srce_addr = sv->p2p_addr;
	replyMsg.dest_port = PORT_ETH;
	replyMsg.dest_addr = sv->eth_addr;
	replyMsg.length = sizeof(sdp_hdr_t);	// it's fix!

	// prepare the result data
	resultMsg.flags = 0x07;
	resultMsg.tag = SDP_TAG_RESULT;
	// what if:
	// - srce_addr contains image line number + rgb info
	// - srce_port contains the data sequence
	//resultMsg.srce_port = myCoreID;		// during sending, this must be modified
	//resultMsg.srce_addr = sv->p2p_addr;
	resultMsg.dest_port = PORT_ETH;
	resultMsg.dest_addr = sv->eth_addr;
	//resultMsg.length = ??? --> need to be modified later

	// and the debug data
	debugMsg.flags = 0x07;
	debugMsg.tag = SDP_TAG_DEBUG;
	debugMsg.srce_port = (SDP_PORT_CONFIG << 5) + myCoreID;
	debugMsg.srce_addr = sv->p2p_addr;
	debugMsg.dest_port = PORT_ETH;
	debugMsg.dest_addr = sv->eth_addr;
	debugMsg.length = sizeof(sdp_hdr_t) + sizeof(cmd_hdr_t);
}

// initImage() should be called by leadAp to initialize buffers
void initImage()
{
	blkInfo->imageInfoRetrieved = 0;
	blkInfo->fullRImageRetrieved = 0;
	blkInfo->fullGImageRetrieved = 0;
	blkInfo->fullBImageRetrieved = 0;
	blkInfo->imgRIn = (uchar *)IMG_R_BUFF0_BASE;
	blkInfo->imgGIn = (uchar *)IMG_G_BUFF0_BASE;
	blkInfo->imgBIn = (uchar *)IMG_B_BUFF0_BASE;
	blkInfo->imgOut1 = (uchar *)IMG_O_BUFF1_BASE;
	blkInfo->imgOut2 = (uchar *)IMG_O_BUFF2_BASE;
	blkInfo->imgOut3 = (uchar *)IMG_O_BUFF3_BASE;
	dtcmImgBuf = NULL;
	pixelCntr = 0;  // for forwarding using MCPL
}

void initIPTag()
{
	// only chip <0,0>
	if(sv->p2p_addr==0) {
		sdp_msg_t iptag;
		iptag.flags = 0x07;	// no replay
		iptag.tag = 0;		// internal
		iptag.srce_addr = sv->p2p_addr;
		iptag.srce_port = 0xE0 + myCoreID;	// use port-7
		iptag.dest_addr = sv->p2p_addr;
		iptag.dest_port = 0;				// send to "root"
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
	}
}
