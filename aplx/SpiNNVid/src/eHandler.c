#include "SpiNNVid.h"

/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*------------------- Main handler functions ---------------------*/

// forward declaration:
void configure_network(uint mBox);



void hDMA(uint tid, uint tag)
{
	//io_printf(IO_BUF, "DMA done tag-0x%x tid-%d\n", tag, tid);
	if((tag & 0xFFFF) == DMA_FETCH_IMG_TAG) {
		if((tag >> 16) == myCoreID) {
			//io_printf(IO_BUF, "Reseting dmaImgFromSDRAMdone for core-%d\n", myCoreID);
			dmaImgFromSDRAMdone = 1;	// so the image processing can continue
		}
	}

}



void hMCPL(uint key, uint payload)
{
	uint key_hdr = 0xFFFF0000 & key;
	uint key_arg = 0xFFFF & key;
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
	else if(key==MCPL_BCAST_GET_WLOAD) {
		spin1_schedule_callback(computeWLoad, 0, 0, PRIORITY_PROCESSING);
	}

	/*------------------------------ Di museum kan ------------------------------
	/*----- special for core 2,3 and 4 -----*
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
	*/

	// outside root node: make it core-to-core communication
	// the following 5 else-if are used to transmit a chunk (270 pixels)
	else if(key_hdr == MCPL_FWD_PIXEL_INFO && key_arg == myCoreID) {
		pxBuffer.pxLen = payload >> 16;
		pxBuffer.pxSeq = payload & 0xFFFF;
		pxBuffer.pxCntr[0] = 0;
		pxBuffer.pxCntr[1] = 0;
		pxBuffer.pxCntr[2] = 0;
		pxBuffer.pxCntr[3] = 0;
		/*
		if(sv->p2p_addr==1)
			io_printf(IO_STD, "Got MCPL_FWD_PIXEL_INFO\n");
		else
			io_printf(IO_BUF, "Got MCPL_FWD_PIXEL_INFO\n");
			*/
	}
	else if(key_hdr == MCPL_FWD_PIXEL_RDATA && key_arg == myCoreID) {
		//sark_mem_cpy((void *)pxBuffer.rpxbuf + pxBuffer.pxCntr[0]*sizeof(uint),
		//			 (void *)&payload, sizeof(uint));
		sark_mem_cpy((void *)rpxbuf + pxBuffer.pxCntr[0]*sizeof(uint),
					 (void *)&payload, sizeof(uint));
		pxBuffer.pxCntr[0]++;
	}
	else if(key_hdr == MCPL_FWD_PIXEL_GDATA && key_arg == myCoreID) {
		//sark_mem_cpy((void *)pxBuffer.gpxbuf + pxBuffer.pxCntr[1]*sizeof(uint),
		//			 (void *)&payload, sizeof(uint));
		sark_mem_cpy((void *)gpxbuf + pxBuffer.pxCntr[1]*sizeof(uint),
					 (void *)&payload, sizeof(uint));
		pxBuffer.pxCntr[1]++;
	}
	else if(key_hdr == MCPL_FWD_PIXEL_BDATA && key_arg == myCoreID) {
		//sark_mem_cpy((void *)pxBuffer.bpxbuf + pxBuffer.pxCntr[2]*sizeof(uint),
		//			 (void *)&payload, sizeof(uint));
		sark_mem_cpy((void *)bpxbuf + pxBuffer.pxCntr[2]*sizeof(uint),
					 (void *)&payload, sizeof(uint));
		pxBuffer.pxCntr[2]++;
	}
	else if(key_hdr == MCPL_FWD_PIXEL_YDATA && key_arg == myCoreID) {
		//sark_mem_cpy((void *)pxBuffer.bpxbuf + pxBuffer.pxCntr[2]*sizeof(uint),
		//			 (void *)&payload, sizeof(uint));
		sark_mem_cpy((void *)ypxbuf + pxBuffer.pxCntr[3]*sizeof(uint),
					 (void *)&payload, sizeof(uint));
		pxBuffer.pxCntr[3]++;
	}
	else if(key_hdr == MCPL_FWD_PIXEL_EOF && key_arg == myCoreID) {
		// debug 29.07.2016: since we only broadcast gray, no need to process it
#if (FWD_FULL_COLOR==TRUE)
		spin1_schedule_callback(processGrayScaling, 0, 0, PRIORITY_PROCESSING);
#else
		spin1_schedule_callback(collectGrayPixels, 0, 0, PRIORITY_PROCESSING);
#endif
	}

	/*---------------- Old processing mechanism for forwarded packet ------------------
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
	*/


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

	else if(key==MCPL_EDGE_DONE) {
		nEdgeJobDone++;
		if(nEdgeJobDone==workers.tAvailable) {
			// collect measurement
			// Note: this also includes time for filtering, since filtering also trigger edging
			toc = sv->clock_ms;
			elapse = toc-tic;	// in milliseconds

			// what next?
			spin1_schedule_callback(afterEdgeDone, 0, 0, PRIORITY_PROCESSING);
		}
	}


	/*----------------------------------------------------------------------------*/
	/*----------------------------------------------------------------------------*/
	/*-------------------------- All cores processing ----------------------------*/
	else if(key==MCPL_BCAST_START_PROC) {
		spin1_schedule_callback(triggerProcessing, key, payload, PRIORITY_PROCESSING);
	}
	else if(key==MCPL_BCAST_ALL_REPORT) {
		// MCPL_BCAST_ALL_REPORT will be broadcasted to all cores in the system
		// and the requesting info will be provided in the payload
		spin1_schedule_callback(give_report, payload, 0, PRIORITY_PROCESSING);
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
		//io_printf(IO_STD, "Got frame info...\n");
		// will only send wImg (in cmd_rc) and hImg (in seq)
		blkInfo->wImg = msg->cmd_rc;
		blkInfo->hImg = msg->seq;
		// send frame info to other nodes
		spin1_send_mc_packet(MCPL_BCAST_FRAME_INFO, (msg->cmd_rc << 16) + msg->seq, WITH_PAYLOAD);
		// then broadcast to all workers (including leadAp) to compute their workload
		spin1_send_mc_packet(MCPL_BCAST_GET_WLOAD, 0, WITH_PAYLOAD);
	}

	/*--------------------------------------------------------------------------------------*/
	/*--------------------------------------------------------------------------------------*/
	/*-------------- New revision: several cores may receives frames directly --------------*/
	else if(port==SDP_PORT_R_IMG_DATA) {
		// chCntr++;
		// then tell core-2 to proceed

		//ushort pxLen = msg->length - sizeof(sdp_hdr_t);
		//io_printf(IO_BUF, "hSDP: got mBox at-0x%x with pxLen=%d\n", msg, pxLen);
		//spin1_send_mc_packet(MCPL_PROCEED_R_IMG_DATA, mBox, WITH_PAYLOAD);
		//return;	// exit from here, because msg should be freed by core-2

		//pxBuffer.pxSeq = msg->cmd_rc;		// this is in version 0.1
		//pxBuffer.pxLen = msg->length - 10;	// msg->length - sizeof(sdp_hdr_t) - sizeof(ushort)
		//sark_mem_cpy(pxBuffer.rpxbuf, &msg->seq, pxBuffer.pxLen);
		//sark_mem_cpy(rpxbuf, &msg->seq, pxBuffer.pxLen);
		pxBuffer.pxSeq = msg->srce_addr;
		pxBuffer.pxLen = msg->length - 8;
		//we use srce_addr for pxSeq, hence more data can be loaded into scp
		sark_mem_cpy(rpxbuf, &msg->cmd_rc, pxBuffer.pxLen);

		// NOTE: don't forward yet, the core is still receiving "fast" sdp packets
		// spin1_schedule_callback(fwdImgData, 0, 0, PRIORITY_PROCESSING);

		// Debugging 28.07.2016: how many pxSeq?
		//io_printf(IO_STD, "Got pxSeq-%d with msg->length %d\n", msg->cmd_rc, msg->length);
	}
	else if(port==SDP_PORT_G_IMG_DATA) {
		// chCntr++;
		// then tell core-3 to proceed

		//ushort pxLen = msg->length - sizeof(sdp_hdr_t);
		//io_printf(IO_BUF, "hSDP: got mBox at-0x%x with pxLen=%d\n", msg, pxLen);
		//spin1_send_mc_packet(MCPL_PROCEED_G_IMG_DATA, mBox, WITH_PAYLOAD);
		//return;	// exit from here, because msg should be freed by core-3

		//pxBuffer.pxSeq = msg->cmd_rc;		// not important, if there's packet lost...
		//pxBuffer.pxLen = msg->length - 10;	// msg->length - sizeof(sdp_hdr_t) - sizeof(ushort)
		//sark_mem_cpy(pxBuffer.gpxbuf, &msg->seq, pxBuffer.pxLen);
		//sark_mem_cpy(gpxbuf, &msg->seq, pxBuffer.pxLen);

		pxBuffer.pxSeq = msg->srce_addr;
		pxBuffer.pxLen = msg->length - 8;
		sark_mem_cpy(gpxbuf, &msg->cmd_rc, pxBuffer.pxLen);



		// NOTE: don't forward yet, the core is still receiving "fast" sdp packets
		// spin1_schedule_callback(fwdImgData, 0, 0, PRIORITY_PROCESSING);
		// TODO: Think about packet lost...
		//       Note, pxBuffer.pxSeq and/or pxBuffer.pxLen may be different by now
	}
	else if(port==SDP_PORT_B_IMG_DATA) {
		// chCntr++;
		// then tell core-4 to proceed

		//ushort pxLen = msg->length - sizeof(sdp_hdr_t);
		//io_printf(IO_BUF, "hSDP: got mBox at-0x%x with pxLen=%d\n", msg, pxLen);
		//spin1_send_mc_packet(MCPL_PROCEED_B_IMG_DATA, mBox, WITH_PAYLOAD);
		/*
		// then send reply: NO, IT IS TOO SLOW VIA SDP HANDSHAKING
		if(chCntr==3) {	// all channels have been received
			chCntr = 0;
			spin1_send_sdp_msg(&replyMsg, DEF_SDP_TIMEOUT);
		}
		*/
		//return;	// exit from here, because msg should be freed by core-4
		//pxBuffer.pxSeq = msg->cmd_rc;		// So, if there's packet lost before, it won't be processed!!
		//pxBuffer.pxLen = msg->length - 10;	// msg->length - sizeof(sdp_hdr_t) - sizeof(ushort)
		//sark_mem_cpy(pxBuffer.bpxbuf, &msg->seq, pxBuffer.pxLen);
		//sark_mem_cpy(bpxbuf, &msg->seq, pxBuffer.pxLen);

		pxBuffer.pxSeq = msg->srce_addr;
		pxBuffer.pxLen = msg->length - 8;
		sark_mem_cpy(bpxbuf, &msg->cmd_rc, pxBuffer.pxLen);

		// process gray scalling
		spin1_schedule_callback(processGrayScaling, 0, 0, PRIORITY_PROCESSING);

		// forward and notify to do grayscaling:
		// spin1_schedule_callback(fwdImgData, 0, 0, PRIORITY_PROCESSING);
		// TODO: Note: how to handle missing packet?
	}

	/*--------------------------------------------------------------------------------------*/
	/*--------------------------------------------------------------------------------------*/
	/*-------------- Host will send an empty message at port SDP_PORT_FRAME_END ------------*/
	/*-------------- to trigger spiNNaker to do the processing                  ------------*/
	/*-------------- This command should be sent to core<0,0,1>                 ------------*/
	else if(port==SDP_PORT_FRAME_END) {
		//debugging:
		io_printf(IO_STD, "Processing begin...\n");

		spin1_send_mc_packet(MCPL_BCAST_START_PROC, 0, WITH_PAYLOAD);
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
