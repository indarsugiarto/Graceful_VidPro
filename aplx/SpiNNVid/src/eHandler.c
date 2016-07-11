#include "SpiNNVid.h"

/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*------------------- Main handler functions ---------------------*/

void configure_network(uint mBox);

void hDMA(uint tag, uint tid)
{

}

void hMCPL(uint key, uint payload)
{
	/*----------------- Worker's part ------------------*/
	if(key==MCPL_BCAST_INFO_KEY) {
		// leadAp sends "0" for ping, workers reply with its core
		if(payload==0)
			spin1_send_mc_packet(MCPL_PING_REPLY, myCoreID, WITH_PAYLOAD);
		else
			blkInfo = (block_info_t *)payload;
	}

	/*------------------ LeadAp part -------------------*/
	else if(key==MCPL_PING_REPLY) {
		workers.wID[workers.tAvailable] = payload;
		workers.tAvailable++;

		// if all other cores have been reporting,
		// broadcast their working load ID
		if(workers.tAvailable==blkInfo->Nworkers) {
			spin1_schedule_callback(bcastWID, 0, 0, PRIORITY_PROCESSING);
		}
	}
}

void hSDP(uint mBox, uint port)
{
	// Note: the variable msg might be released somewhere else,
	//       especially when delivering frame's channel
	sdp_msg_t *msg = (sdp_msg_t *)mBox;

	if(port==SDP_PORT_CONFIG) {
		if(msg->cmd_rc == SDP_CMD_CONFIG_NETWORK) {
			// will send: maxBlock, nodeID, op-type
			configure_network(mBox);    // Note: should only by chip<0,0>
		}
	}
	else if(port==SDP_PORT_FRAME_INFO) {
		// will only send wImg and hImg

	}
	else if(port==SDP_PORT_R_IMG_DATA) {

	}
	else if(port==SDP_PORT_G_IMG_DATA) {

	}
	else if(port==SDP_PORT_B_IMG_DATA) {

	}

	// Note: variable msg might be released somewhere else
	spin1_msg_free(msg);
}

void hTimer(uint tick, uint Unused)
{
	if(tick==1) {
		// how many workers are there?
		uchar nCores = get_Nworkers();
		io_printf(IO_BUF, "Found %d active cores\n", nCores);
		// what's my node id?
		io_printf(IO_BUF, "My node-ID is %d out of %d\n", blkInfo->nodeBlockID,
				  blkInfo->maxBlock);
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

		blkInfo->maxBlock = msg->arg1;
		payload = blkInfo->maxBlock << 24;
		// here we build chips database for later propagation usage
		// Note: the other node will use the maxBlock info for counting
		for(ushort i=0; i<msg->arg1; i++) {
			chips[i].id = msg->data[i*3];
			chips[i].x = msg->data[i*3 + 1];
			chips[i].y = msg->data[i*3 + 2];
			payload += (chips[i].id << 16) + (chips[i].x << 8) + (chips[i].y);
			spin1_send_mc_packet(MCPL_BCAST_NODES_INFO, payload, WITH_PAYLOAD);
		}
	}
}

