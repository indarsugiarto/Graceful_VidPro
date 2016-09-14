#include "SpiNNVid.h"

/*------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------*/
/*------------------- Sub functions called by main handler ---------------------*/

void setFreq(uint f, uint null)
{
	// if f is set to 0, then we play the adaptive strategy with initial freq 200MHz
	if(f==0) {
		freq = 200;
		adaptiveFreq = TRUE;
	} else {
		freq = f;
		adaptiveFreq = FALSE;
	}
	changeFreq(freq);

#if (DEBUG_LEVEL>0)
		if(sv->p2p_addr==0)	io_printf(IO_STD, "Set freq to %d\n", f);
#endif
}

void configure_network(uint mBox)
{
#if (DEBUG_LEVEL>2)
	io_printf(IO_STD, "Receiving configuration...\n");
#endif
	sdp_msg_t *msg = (sdp_msg_t *)mBox;
	if(msg->cmd_rc == SDP_CMD_CONFIG_NETWORK) {

		// for safety, reset the network:
		spin1_send_mc_packet(MCPL_BCAST_RESET_NET, 0, WITH_PAYLOAD);

		uint payload;   // for broadcasting
		// encoding strategy:
		// seq = (opType << 8) | (wFilter << 4) | wHistEq;

		blkInfo->opType = msg->seq >> 8;
		blkInfo->opFilter = (msg->seq >> 4) & 0xF;
		blkInfo->opSharpen = msg->seq & 0xF;

		payload = msg->seq;
		// add frequency information
		payload |= ((uint)msg->srce_port << 16);

		// then apply frequency requirement
		// if the freq is 0, then use adaptive mechanism:
		// initially, it runs at 200MHz, but change to 250 during processing
		setFreq(msg->srce_port, NULL);

		// then tell the other nodes about this op-freq info
		spin1_send_mc_packet(MCPL_BCAST_OP_INFO, payload, WITH_PAYLOAD);

		// broadcasting for nodes configuration
		// convention: chip<0,0> must be root, it has the ETH
		if(sv->p2p_addr==0) blkInfo->nodeBlockID = 0;

		// NOTE: we decide to use at least 4 nodes!!!
		// another convention:
		// arg1, arg2, arg3 contain node-1, node-2, node-3
		chips[0].id = 0; chips[0].x = 0; chips[0].y = 0;
		chips[1].id = 1; chips[1].x = msg->arg1 >> 8; chips[1].y = msg->arg1 & 0xFF;
		chips[2].id = 2; chips[2].x = msg->arg2 >> 8; chips[2].y = msg->arg2 & 0xFF;
		chips[3].id = 3; chips[3].x = msg->arg3 >> 8; chips[3].y = msg->arg3 & 0xFF;

		// infer from msg->length
		uchar restNodes = (msg->length - sizeof(sdp_hdr_t) - sizeof(cmd_hdr_t)) / 2;
		uchar maxAdditionalNodes;
#if(USING_SPIN==3)
		maxAdditionalNodes = 0;
#else
		maxAdditionalNodes = 44;
#endif
		if(restNodes > maxAdditionalNodes) {
			terminate_SpiNNVid(IO_DBG, "Invalid network size!\n", RTE_SWERR);
		}
		else {
			if(restNodes > 0) {
				for(uchar i=0; i<restNodes; i++) {
					chips[i+4].id = i+4;
					chips[i+4].x = msg->data[i*2];
					chips[i+4].y = msg->data[i*2 + 1];
				}
			}
			blkInfo->maxBlock = restNodes + 4;	// NOTE: minBlock = 4

			// then broadcast
			// Note: the other node will use the maxBlock info for counting
			for(uchar i=0; i<blkInfo->maxBlock; i++) {
				payload = blkInfo->maxBlock << 24;
				payload += (chips[i].id << 16) + (chips[i].x << 8) + (chips[i].y);
				spin1_send_mc_packet(MCPL_BCAST_NODES_INFO, payload, WITH_PAYLOAD);
			}
		}
	}

	// debugging:
	// give_report(DEBUG_REPORT_BLKINFO, 0);
}


void reportHistToLeader(uint arg0, uint arg1)
{
	if(leadAp) return;
	for(uchar i=0; i<256; i++) {
		spin1_send_mc_packet(MCPL_REPORT_HIST2LEAD + i, hist[i], WITH_PAYLOAD);
	}
}

// in leaderUpdateHist(), leader will check if the node is leaf and send via SDP
// accordingly. Alternatively, leaderUpdateHist() will be called if an SDP arrives
// from children nodes

void propagateHist()
{
	uchar i, *ptr;
	ptr = (uchar *)hist;
	for(i=0; i<sizeof(uint); i++) {
		sark_mem_cpy(histMsg.data, ptr, 256);
		histMsg.arg1 = i;
		spin1_send_sdp_msg(&histMsg, 10);
		ptr += 256;
	}
}

void leaderUpdateHist(uint arg0, uint arg1)
{
	uchar i;
	if(histPropTree.isLeaf) {
		propagateHist();
	}
	else {
		// collect histogram from children
		if(histPropTree.SDPItemCntr == histPropTree.maxHistSDPItem) {
			for(i=0; i<256; i++)
				hist[i] += child1hist[i] + child2hist[i];
			// if not the root-node, go propagate to the next parent
			if(sv->p2p_addr != 0) {
				propagateHist();
			}
			// if root-node, stop the propagation and broadcast the histogram
			else {
				maxHistValue = 0;
				for(i=0; i<256; i++) {
					maxHistValue += hist[i];
					spin1_send_mc_packet(MCPL_BCAST_HIST_RESULT+i, hist[i], WITH_PAYLOAD);
				}
				// then measure time
				perf.tHistProp = READ_TIMER();
#if (DEBUG_LEVEL > 0)
				io_printf(IO_STD, "Histogram propagation clock period = %u\n",
						  perf.tHistProp);
#endif
			}
		}
	}
}

/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*------------------- Main handler functions ---------------------*/

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



void hMCPL_SpiNNVid(uint key, uint payload)
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
		blkInfo->opFilter = (payload >> 4) & 0xF;
		blkInfo->opSharpen = payload & 0xF;
		spin1_schedule_callback(setFreq, (payload >> 16), NULL, PRIORITY_PROCESSING);
	}
	else if(key==MCPL_BCAST_NODES_INFO) {
		// how to disable default node-ID to prevent a chip accidently active due to its
		// default ID (during first time call with get_block_id() ?
		// one possible solution: using MCPL_BCAST_RESET_NET, with which
		// the maxBlock is set to 0 and nodeBlockID to 0xFF
		// Then we set it here if the payload is for us

		uchar id = (payload >> 16) & 0xFF;
		uchar x = (payload >> 8) & 0xFF;
		uchar y = (payload & 0xFF);

		// put the list, so that it can be used in the future (eg. for histogram)
		chips[id].id = id; chips[id].x = x; chips[id].y = y;

		// now check if the payload contains information for me
		if(x==blkInfo->myX && y==blkInfo->myY) {
			blkInfo->nodeBlockID = id;
			blkInfo->maxBlock = payload >> 24;
			// debugging:
			// give_report(DEBUG_REPORT_BLKINFO, 0);
		}
	}
	else if(key==MCPL_BCAST_RESET_NET) {
#if (DEBUG_LEVEL>2)
		io_printf(IO_BUF, "Got net reset cmd\n");
#endif
		blkInfo->maxBlock = 0;
		blkInfo->nodeBlockID = 0xFF;
	}

	// got frame info from root node, then broadcast to workers
	else if(key==MCPL_BCAST_FRAME_INFO) {
		blkInfo->wImg = payload >> 16;
		blkInfo->hImg = payload & 0xFFFF;
		// then broadcast to all workers (including leadAp) to compute their workload
		spin1_send_mc_packet(MCPL_BCAST_GET_WLOAD, 0, WITH_PAYLOAD);
	}

	// MCPL_EDGE_DONE is sent by every core to leadAp
	else if(key==MCPL_EDGE_DONE) {
		// the payload carries information about timing
		nEdgeJobDone++;
		perf.tEdgeNode += payload;

		// I think we have a problem with comparing to tAvailable,
		// because not all cores might be used (eg. the frame is small)
		if(nEdgeJobDone==workers.tRunning) {
			perf.tEdgeNode /= nEdgeJobDone;
			// collect measurement
			// Note: this also includes time for filtering, since filtering also trigger edging
			toc = sv->clock_ms;
			elapse = toc-tic;	// in milliseconds

			// what next? trigger the chain for sending from root-node
			spin1_schedule_callback(afterProcessingDone, 0, 0, PRIORITY_PROCESSING);
		}
	}

	// MCPL_BLOCK_DONE will be sent by leadAp in each node (including the leadAp in the root-node)
	// to core <0,0,leadAp>
	else if(key==MCPL_BLOCK_DONE) {
		nBlockDone++;
		/*
		io_printf(IO_STD, "Receive MCPL_BLOCK_DONE from node-%d\n", payload);
		io_printf(IO_STD, "Total nBlockDone now is %d\n", nBlockDone);
		io_printf(IO_STD, "Next block should be %d\n", payload+1);
		*/
		if(nBlockDone==blkInfo->maxBlock) {
			spin1_schedule_callback(notifyDestDone, 0, 0, PRIORITY_PROCESSING);
		}
		// if I'm block-0, continue the chain by broadcasting to other nodes
		// with the next node-ID (++payload)
		// MCPL_BCAST_SEND_RESULT is destined to other external nodes from root-node
		else if(blkInfo->nodeBlockID==0) {
			spin1_send_mc_packet(MCPL_BCAST_SEND_RESULT, payload+1, WITH_PAYLOAD);
			//spin1_send_mc_packet(MCPL_BCAST_SEND_RESULT, ++payload, WITH_PAYLOAD);
		}
	}
	// id addition to MCPL_BLOCK_DONE, a node might send its performance measure
	else if(key==MCPL_BLOCK_DONE_TEDGE) {
		perf.tEdgeTotal += payload;
	}

	else if(key==MCPL_BCAST_SEND_RESULT) {
#if (DESTINATION==DEST_HOST)
		spin1_schedule_callback(sendDetectionResult2Host, payload, 0, PRIORITY_PROCESSING);
#elif (DESTINATION==DEST_FPGA)
		spin1_schedule_callback(sendDetectionResult2FPGA, payload, 0, PRIORITY_PROCESSING);
#endif
	}

	/*----------------------------------------------------------------------------*/
	/*----------------------------------------------------------------------------*/
	/*----------------- Certain cores for histogram processing -------------------*/
	// NOTE: during gray pixel forwarding, it involves almost all cores
	// hence, when a core receive MCPL_BCAST_REPORT_HIST:
	// 1. it forwards its histogram content to leadAp
	// 2. leadAp then collect all histogram from workers and combine them into
	//    one unified histogram of the node
	// 3. the leadAp then forward this unified histogram if it is a leaf node
	//    Otherwise, it will wait the children nodes to send their histrogram
	//MCPL_BCAST_REPORT_HIST

	else if(key == MCPL_BCAST_REPORT_HIST) {
		// each core, EXCEPT the leadAP, needs to report its histogram to leadAp:
		spin1_schedule_callback(reportHistToLeader, 0, 0, PRIORITY_PROCESSING);
	}
	else if(key_hdr == MCPL_REPORT_HIST2LEAD) {
		hist[key_arg] += payload;
		histPropTree.MCPLItemCntr++;
		if(histPropTree.MCPLItemCntr == histPropTree.maxHistMCPLItem) {
			spin1_schedule_callback(leaderUpdateHist, 0, 0, PRIORITY_PROCESSING);
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
	/*
	io_printf(IO_STD, "got sdp tag = 0x%x, srce_port = 0x%x, srce_addr = 0x%x, dest_port = 0x%x\n",
			  msg->tag, msg->srce_port, msg->srce_addr, msg->dest_port);
	*/
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
		else if(msg->cmd_rc == SDP_CMD_RESET_NETWORK) {
#if (DEBUG_LEVEL>2)
			io_printf(IO_STD, "Got net reset cmd\n");
#endif
			// ignore this command if using fixed nodes configuration
#ifndef USE_FIX_NODES
			// then broadcast reset network to make maxBlock 0 and nodeBlockID 0xFF
			spin1_send_mc_packet(MCPL_BCAST_RESET_NET, 0, WITH_PAYLOAD);
#endif
		}
		else if(msg->cmd_rc == SDP_CMD_FRAME_INFO) {
			//io_printf(IO_STD, "Got frame info...\n");
			// will only send wImg (in arg1.high) and hImg (in arg1.low)
			blkInfo->wImg = msg->arg1 >> 16;
			blkInfo->hImg = msg->arg1 & 0xFFFF;
			// send frame info to other nodes
			spin1_send_mc_packet(MCPL_BCAST_FRAME_INFO, msg->arg1, WITH_PAYLOAD);
			// then broadcast to all workers (including leadAp) to compute their workload
			spin1_send_mc_packet(MCPL_BCAST_GET_WLOAD, 0, WITH_PAYLOAD);
		}
	}

	/*--------------------------------------------------------------------------------------*/
	/*--------------------------------------------------------------------------------------*/
	/*-------------- New revision: several cores may receives frames directly --------------*/

	// NOTE: srce_addr TIDAK BISA DIPAKAI UNTUK pxSeq !!!!!
	// Karena srce_addr PASTI bernilai 0 jika dikirim dari host-PC

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

		//pxBuffer.pxSeq = msg->srce_addr;
		pxBuffer.pxSeq = (msg->tag << 8) | msg->srce_port;
		pxBuffer.pxLen = msg->length - 8;
		//we use srce_addr for pxSeq, hence more data can be loaded into scp
		sark_mem_cpy(rpxbuf, &msg->cmd_rc, pxBuffer.pxLen);


		///io_printf(IO_STD, "pxSeq = %d\n", pxBuffer.pxSeq);

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

		//pxBuffer.pxSeq = msg->srce_addr;
		pxBuffer.pxSeq = (msg->tag << 8) | msg->srce_port;
		pxBuffer.pxLen = msg->length - 8;
		sark_mem_cpy(gpxbuf, &msg->cmd_rc, pxBuffer.pxLen);

		//io_printf(IO_STD, "pxSeq = %d\n", pxBuffer.pxSeq);


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

		//pxBuffer.pxSeq = msg->srce_addr;
		pxBuffer.pxSeq = (msg->tag << 8) | msg->srce_port;
		pxBuffer.pxLen = msg->length - 8;
		sark_mem_cpy(bpxbuf, &msg->cmd_rc, pxBuffer.pxLen);


		//io_printf(IO_STD, "pxSeq = %d\n", pxBuffer.pxSeq);


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

		// Karena srce_addr tidak bisa dipakai untuk pxSeq, kita buat pxSeq secara
		// sekuensial:
		// pxBuffer.pxSeq = 0;	// dan juga harus di-reset di bagian SDP_PORT_FRAME_INFO

#if (DEBUG_LEVEL > 0)
		//debugging:
		io_printf(IO_STD, "Processing begin...\n");
#endif
		spin1_send_mc_packet(MCPL_BCAST_START_PROC, 0, WITH_PAYLOAD);
	}

	else if(port==SDP_PORT_FPGA_OUT) {
		if(msg->cmd_rc==SDP_CMD_SEND_PX_FPGA) {
			// TODO: extract the data and convert into fix-route
			// The sending uses "special" modification of the header:
			// resultMsg.srce_addr = lines;	// is it useful??? for debugging!!!
			// resultMsg.srce_port = c;	// is it useful???
			// When all lines has been sent, the following will be sent SDP_SRCE_NOTIFY_PORT
			// by chip<0,0>. So, we don't need this!
			ushort x, c, i, l;
			uchar px;
			uchar *p;
			ushort y = msg->srce_addr;
			if(msg->srce_port!=SDP_SRCE_NOTIFY_PORT) {
				l = msg->length-sizeof(sdp_hdr_t);
				c = msg->srce_port;
				p = (uchar *)&msg->cmd_rc;
				for(i=0; i<l; i++) {
					x = c*272+i;
					px = *(p+i);
					// we don't need to modify msb of x and y because we send gray image
					// in the future, we might change this!
					// now we have [x,y,px]
					uint key, dest = 1 << 4;	// NOTE: going to link-4
					rtr_fr_set(dest);
					key = (((uint)x << 16) | y) & 0x7FFF7FFF;
					spin1_send_fr_packet(key, px, WITH_PAYLOAD);
				}
			}
		}
	}

	else if(port==SDP_PORT_HISTO) {
		if(msg->cmd_rc==SDP_CMD_REPORT_HIST) {
			uchar *ptr;
			if(msg->seq == histPropTree.c[0]) {
				ptr = (uchar *)child1hist;
			}
			else {
				ptr = (uchar *)child2hist;
			}

			// then copy the sdp data
			ptr += msg->arg1*256;
			sark_mem_cpy(ptr, msg->data, 256);

			// then check if all children have reported their histogram
			histPropTree.SDPItemCntr++;
			spin1_schedule_callback(leaderUpdateHist, 0, 0, PRIORITY_PROCESSING);
		}
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

