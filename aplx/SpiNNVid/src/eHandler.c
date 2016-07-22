#include "SpiNNVid.h"

/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*------------------- Main handler functions ---------------------*/

// forward declaration:
void configure_network(uint mBox);



void hDMA(uint tag, uint tid)
{

}



void hMCPL(uint key, uint payload)
{
	/*----------------------------------------------------------------------------*/
	/*----------------------------------------------------------------------------*/
	/*---------------------------- Worker's part ---------------------------------*/
	if(key==MCPL_BCAST_INFO_KEY) {
		// leadAp sends "0" for ping, workers reply with its core
		if(payload==0)
			spin1_send_mc_packet(MCPL_PING_REPLY, myCoreID, WITH_PAYLOAD);
		else // otherwise, leadAp send blkInfo address
			blkInfo = (block_info_t *)payload;
	}
	// leadAp sends workload ID to workers
	else if(key==myCoreID) {
		workers.tAvailable = payload >> 16;
		workers.subBlockID = payload & 0xFFFF;
		spin1_schedule_callback(give_report, DEBUG_REPORT_MYWID, 0, PRIORITY_PROCESSING);
	}
	// host sends request for report via leadAp
	else if(key==MCPL_BCAST_ALL_REPORT) {
		// MCPL_BCAST_ALL_REPORT will be broadcasted to all cores in the system
		// and the requesting info will be provided in the payload
		spin1_schedule_callback(give_report, payload, 0, PRIORITY_PROCESSING);
	}
	else if(key==MCPL_BCAST_GET_WLOAD) {
		spin1_schedule_callback(computeWLoad, 0, 0, PRIORITY_PROCESSING);
	}
	/*----- special for core 2,3 and 4 -----*/
	// within root node
	else if(key==MCPL_PROCEED_R_IMG_DATA) {
		spin1_schedule_callback(processImgData, payload, 0, PRIORITY_PROCESSING);
	}
	else if(key==MCPL_PROCEED_G_IMG_DATA) {
		spin1_schedule_callback(processImgData, payload, 1, PRIORITY_PROCESSING);
	}
	else if(key==MCPL_PROCEED_B_IMG_DATA) {
		spin1_schedule_callback(processImgData, payload, 2, PRIORITY_PROCESSING);
	}
	// outside root node
	// payload contains the forward-ed packet from root node (either from core 2, 3 or 4)
	// the lower nibble of the key contains pxLen
	else if((key & 0xFFFF0000) == MCPL_FWD_R_IMG_DATA) {
		uint pxLenCh = 0x00000000 | (key & 0xFFFF);
		spin1_schedule_callback(recvFwdImgData, payload, pxLenCh, PRIORITY_PROCESSING);
	}
	else if((key & 0xFFFF0000) == MCPL_FWD_G_IMG_DATA) {
		uint pxLenCh = 0x00010000 | (key & 0xFFFF);
		spin1_schedule_callback(recvFwdImgData, payload, pxLenCh, PRIORITY_PROCESSING);
	}
	else if((key & 0xFFFF0000) == MCPL_FWD_B_IMG_DATA) {
		uint pxLenCh = 0x00020000 | (key & 0xFFFF);
		spin1_schedule_callback(recvFwdImgData, payload, pxLenCh, PRIORITY_PROCESSING);
	}

	/*----------------------------------------------------------------------------*/
	/*----------------------------------------------------------------------------*/
	/*------------------------------- LeadAp part --------------------------------*/
	// workers reply the ping
	else if(key==MCPL_PING_REPLY) {
		workers.wID[workers.tAvailable] = payload;
		workers.tAvailable++;

		// if all other cores have been reporting,
		// broadcast their working load ID and blkInfo
		if(workers.tAvailable==blkInfo->Nworkers) {
			spin1_schedule_callback(bcastWID, 0, 0, PRIORITY_PROCESSING);
		}
	}
	else if(key==MCPL_BCAST_OP_INFO) {
		blkInfo->opType = payload >> 8;
		blkInfo->opFilter = payload & 0xFF;
	}
	else if(key==MCPL_BCAST_NODES_INFO) {
		// how to disable default node-ID to prevent a chip accidently active due to its
		// default ID (during first time call with get_block_id() ?
		// one possible solution:
		// if we get MCPL_BCAST_NODES_INFO, first we reset maxBlock to 0
		// then we set it again if the payload is for us

		uchar id = (payload >> 16) & 0xFF;
		uchar x = (payload >> 8) & 0xFF;
		uchar y = (payload & 0xFF);
		// now check if the payload contains information for me
		if(x==blkInfo->myX && y==blkInfo->myY) {
			blkInfo->nodeBlockID = id;
			blkInfo->maxBlock = payload >> 24;
		}
	}
	// got frame info from root node, then broadcast to workers
	else if(key==MCPL_BCAST_FRAME_INFO) {
		blkInfo->wImg = payload >> 16;
		blkInfo->hImg = payload & 0xFFFF;
		// then broadcast to all workers (including leadAp) to compute their workload
		spin1_send_mc_packet(MCPL_BCAST_GET_WLOAD, 0, WITH_PAYLOAD);
	}
}



void hSDP(uint mBox, uint port)
{
	// Note: the variable msg might be released somewhere else,
	//       especially when delivering frame's channel
	sdp_msg_t *msg = (sdp_msg_t *)mBox;
	// io_printf(IO_STD, "got sdp cmd_rc=%d, seq=%d...\n", msg->cmd_rc, msg->seq);

	if(port==SDP_PORT_CONFIG) {
		if(msg->cmd_rc == SDP_CMD_CONFIG_NETWORK) {
			// will send: maxBlock, nodeID, op-type
			configure_network(mBox);    // Note: should only by chip<0,0>
		}
		else if(msg->cmd_rc == SDP_CMD_GIVE_REPORT) {
			// will use MCPL_BCAST_ALL_REPORT to all cores in the system
			// and the requesting info will be provided in the seq
			spin1_send_mc_packet(MCPL_BCAST_ALL_REPORT, msg->seq, WITH_PAYLOAD);
		}
	}
	else if(port==SDP_PORT_FRAME_INFO) {
		// will only send wImg (in cmd_rc) and hImg (in seq)
		blkInfo->wImg = msg->cmd_rc;
		blkInfo->hImg = msg->seq;
		// send frame info to other nodes
		spin1_send_mc_packet(MCPL_BCAST_FRAME_INFO, (msg->cmd_rc << 16) + msg->seq, WITH_PAYLOAD);
		// then broadcast to all workers (including leadAp) to compute their workload
		spin1_send_mc_packet(MCPL_BCAST_GET_WLOAD, 0, WITH_PAYLOAD);
	}
	// in this version, the frame is compressed using RLE
	else if(port==SDP_PORT_R_IMG_DATA) {
		// chCntr++;
		// then tell core-2 to proceed
		spin1_send_mc_packet(MCPL_PROCEED_R_IMG_DATA, mBox, WITH_PAYLOAD);
		return;	// exit from here, because msg should be freed by core-2
	}
	else if(port==SDP_PORT_G_IMG_DATA) {
		// chCntr++;
		// then tell core-3 to proceed
		spin1_send_mc_packet(MCPL_PROCEED_G_IMG_DATA, mBox, WITH_PAYLOAD);
		return;	// exit from here, because msg should be freed by core-3
	}
	else if(port==SDP_PORT_B_IMG_DATA) {
		// chCntr++;
		// then tell core-4 to proceed
		spin1_send_mc_packet(MCPL_PROCEED_B_IMG_DATA, mBox, WITH_PAYLOAD);
		/*
		// then send reply: NO, IT IS TOO SLOW VIA SDP HANDSHAKING
		if(chCntr==3) {	// all channels have been received
			chCntr = 0;
			spin1_send_sdp_msg(&replyMsg, DEF_SDP_TIMEOUT);
		}
		*/
		return;	// exit from here, because msg should be freed by core-4
	}

	// Note: variable msg might be released somewhere else
	spin1_msg_free(msg);
}

void hTimer(uint tick, uint Unused)
{
	if(tick==1) {
		// how many workers are there?
		spin1_schedule_callback(give_report, DEBUG_REPORT_NWORKERS, 0, PRIORITY_PROCESSING);
	}
}

/*------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------*/
/*------------------- Sub functions called by main handler ---------------------*/

void configure_network(uint mBox)
{
	sdp_msg_t *msg = (sdp_msg_t *)mBox;
	if(msg->cmd_rc == SDP_CMD_CONFIG_NETWORK) {
		uint payload;   // for broadcasting
		// encoding strategy:
		// seq.high:  edge type, 0 = SOBEL, 1 = LAPLACE
		// seq.low: perform filtering? 0 = NO, 1 = YES
		// arg1: max num of blocks
		blkInfo->opType = msg->seq >> 8;
		blkInfo->opFilter = msg->seq & 0xFF;
		payload = msg->seq;
		spin1_send_mc_packet(MCPL_BCAST_OP_INFO, payload, WITH_PAYLOAD);

		// additional broadcasting for nodes configuration if not USE_FIX_NODES
#ifndef USE_FIX_NODES
		blkInfo->maxBlock = msg->arg1;
		// here we build chips database for later propagation usage
		// Note: the other node will use the maxBlock info for counting
		for(uchar i=0; i<msg->arg1; i++) {
			payload = blkInfo->maxBlock << 24;
			chips[i].id = msg->data[i*3];
			chips[i].x = msg->data[i*3 + 1];
			chips[i].y = msg->data[i*3 + 2];
			payload += (chips[i].id << 16) + (chips[i].x << 8) + (chips[i].y);
			// find what's the node-id of my chip
			if(chips[i].x==blkInfo->myX && chips[i].y==blkInfo->myY) {
				blkInfo->nodeBlockID = chips[i].id;
			}
			spin1_send_mc_packet(MCPL_BCAST_NODES_INFO, payload, WITH_PAYLOAD);
		}
#endif
	}
}
