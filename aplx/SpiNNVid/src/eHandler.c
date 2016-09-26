#include "SpiNNVid.h"

/*------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------*/
/*------------------- Sub functions called by main handler ---------------------*/

void build_task_list()
{
	uchar n = 0;
	if(blkInfo->opFilter==1) {
		taskList.tasks[n] = PROC_FILTERING;
		n++;
	}
	if(blkInfo->opSharpen==1) {
		taskList.tasks[n] = PROC_SHARPENING;
		n++;
	}
	if(blkInfo->opType > 0) {
		taskList.tasks[n] = PROC_EDGING_DVS;
		n++;
	}
	// finally, send the result: it's mandatory!
	taskList.tasks[n] = PROC_SEND_RESULT;
	n++;

	taskList.nTasks = n;
	taskList.cTaskPtr = 0;
	taskList.cTask = taskList.tasks[0];
}

void configure_network(uint mBox)
{
	io_printf(IO_STD, "[CONFIG] Got configuration...\n");
	sdp_msg_t *msg = (sdp_msg_t *)mBox;
	if(msg->cmd_rc == SDP_CMD_CONFIG_NETWORK) {
		// NOTE: encoding strategy:
		//		 seq = (opType << 8) | (wFilter << 4) | wHistEq;

		// for safety, reset the network:
		spin1_send_mc_packet(MCPL_BCAST_RESET_NET, 0, WITH_PAYLOAD);

		uint payload;   // also for broadcasting

		// then apply frequency requirement
		// if the freq is 0, then use adaptive mechanism:
		// initially, it runs at 200MHz, but change to 250 during processing
		// setFreq(msg->srce_port, NULL); --> obsolete!
		payload = (msg->srce_port << 16) | PROF_MSG_SET_FREQ;
		spin1_send_mc_packet(MCPL_TO_ALL_PROFILER, payload, WITH_PAYLOAD);

		// then tell the other nodes about this op-freq info

		blkInfo->opType = msg->seq >> 8;
		blkInfo->opFilter = (msg->seq >> 4) & 0xF;
		blkInfo->opSharpen = msg->seq & 0xF;

		payload = msg->seq;
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

	/* Regarding task list.
	 **/
	// first, build a task list:
	build_task_list();

	// debugging:
	// give_report(DEBUG_REPORT_BLKINFO, 0);
}


void reportHistToLeader(uint arg0, uint arg1)
{
	if(myCoreID==LEAD_CORE) return;
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
				/*
				perf.tHistProp = READ_TIMER();
#if (DEBUG_LEVEL > 0)
				io_printf(IO_STD, "Histogram propagation clock period = %u\n",
						  perf.tHistProp);
#endif
				*/
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
	uint key = tag & 0xFFFF;
	uint core = tag >> 16;
	if(key == DMA_FETCH_IMG_TAG) {
		if(core == myCoreID) {
			//io_printf(IO_BUF, "Reseting dmaImgFromSDRAMdone for core-%d\n", myCoreID);
			dmaImgFromSDRAMdone = 1;	// so the image processing can continue
		}
	}
	else if(key == DMA_TAG_STORE_R_PIXELS) {
		blkInfo->dmaDone_rpxStore = core;
	}
	else if(key == DMA_TAG_STORE_G_PIXELS) {
		blkInfo->dmaDone_gpxStore = core;
	}
	else if(key == DMA_TAG_STORE_B_PIXELS) {
		blkInfo->dmaDone_bpxStore = core;
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
	// the following 5 else-if are used to transmit a chunk (272 pixels)
	else if(key_hdr == MCPL_FWD_PIXEL_INFO && key_arg == myCoreID) {
		// for forwarding, exclude root-node!!!!
		if(sv->p2p_addr != 0) {
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
	}
	else if(key_hdr == MCPL_FWD_PIXEL_RDATA && key_arg == myCoreID) {
		// for forwarding, exclude root-node!!!!
		if(sv->p2p_addr != 0) {
			//sark_mem_cpy((void *)pxBuffer.rpxbuf + pxBuffer.pxCntr[0]*sizeof(uint),
			//			 (void *)&payload, sizeof(uint));
			sark_mem_cpy((void *)rpxbuf + pxBuffer.pxCntr[0]*sizeof(uint),
						 (void *)&payload, sizeof(uint));
			pxBuffer.pxCntr[0]++;
		}
	}
	else if(key_hdr == MCPL_FWD_PIXEL_GDATA && key_arg == myCoreID) {
		// for forwarding, exclude root-node!!!!
		if(sv->p2p_addr != 0) {
			//sark_mem_cpy((void *)pxBuffer.gpxbuf + pxBuffer.pxCntr[1]*sizeof(uint),
			//			 (void *)&payload, sizeof(uint));
			sark_mem_cpy((void *)gpxbuf + pxBuffer.pxCntr[1]*sizeof(uint),
					(void *)&payload, sizeof(uint));
			pxBuffer.pxCntr[1]++;
		}
	}
	else if(key_hdr == MCPL_FWD_PIXEL_BDATA && key_arg == myCoreID) {
		// for forwarding, exclude root-node!!!!
		if(sv->p2p_addr != 0) {
			//sark_mem_cpy((void *)pxBuffer.bpxbuf + pxBuffer.pxCntr[2]*sizeof(uint),
			//			 (void *)&payload, sizeof(uint));
			sark_mem_cpy((void *)bpxbuf + pxBuffer.pxCntr[2]*sizeof(uint),
					(void *)&payload, sizeof(uint));
			pxBuffer.pxCntr[2]++;
		}
	}
	else if(key_hdr == MCPL_FWD_PIXEL_YDATA && key_arg == myCoreID) {
		// for forwarding, exclude root-node!!!!
		if(sv->p2p_addr != 0) {
			//sark_mem_cpy((void *)pxBuffer.bpxbuf + pxBuffer.pxCntr[2]*sizeof(uint),
			//			 (void *)&payload, sizeof(uint));
			sark_mem_cpy((void *)ypxbuf + pxBuffer.pxCntr[3]*sizeof(uint),
					(void *)&payload, sizeof(uint));
			pxBuffer.pxCntr[3]++;
		}
	}
	else if(key_hdr == MCPL_FWD_PIXEL_EOF && key_arg == myCoreID) {
		// for forwarding, exclude root-node!!!!
		if(sv->p2p_addr != 0) {
			// debug 29.07.2016: since we only broadcast gray, no need to process it
#if (FWD_FULL_COLOR==TRUE)
			spin1_schedule_callback(processGrayScaling, 0, 0, PRIORITY_PROCESSING);
#else
			spin1_schedule_callback(collectGrayPixels, 0, 0, PRIORITY_PROCESSING);
#endif
		}
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


	/*---------------------------------------------------------------------------*/
	/*------------------ Regarding network and task configuration ---------------*/
	else if(key==MCPL_BCAST_OP_INFO) {
		blkInfo->opType = payload >> 8;
		blkInfo->opFilter = (payload >> 4) & 0xF;
		blkInfo->opSharpen = payload & 0xF;
		//we remove setFreq, because now profiler runs on its own:
		//spin1_schedule_callback(setFreq, (payload >> 16), NULL, PRIORITY_PROCESSING);
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
	/*------------------ End of network and task configuration ----------------*/
	/*-------------------------------------------------------------------------*/



	// got frame info from root node, then broadcast to workers
	else if(key==MCPL_BCAST_FRAME_INFO) {
		blkInfo->wImg = payload >> 16;
		blkInfo->hImg = payload & 0xFFFF;

		// then allocate image buffers in SDRAM
		allocateImgBuf();

		// then broadcast to all workers (including leadAp) to compute their workload
		spin1_send_mc_packet(MCPL_BCAST_GET_WLOAD, 0, WITH_PAYLOAD);
	}

	// MCPL_EDGE_DONE is sent by every core to leadAp within a node
	else if(key==MCPL_EDGE_DONE) {
		// the payload carries information about timing
		//nEdgeJobDone++; --> deprecated!!!
		nWorkerDone++;
		perf.tNode += payload;

		// I think we have a problem with comparing to tAvailable,
		// because not all cores might be used (eg. the frame is small)
		if(nWorkerDone==workers.tRunning) {
			perf.tNode /= nWorkerDone;
			// collect measurement
			//toc = sv->clock_ms;
			//elapse = toc-tic;	// in milliseconds

			// send notification to <0,0,LEAD_CORE> with ID and performance measurement
			uint key = MCPL_BLOCK_DONE_TEDGE | blkInfo->nodeBlockID;
			spin1_send_mc_packet(key, perf.tNode, WITH_PAYLOAD);

			// and tell profiler that processing is done for a node
			spin1_send_mc_packet(MCPL_TO_OWN_PROFILER, PROF_MSG_PROC_END, WITH_PAYLOAD);
		}
	}

	// MCPL_BLOCK_DONE_TEDGE is sent by each node to <0,0,LEAD_CORE>,
	// which contains node-ID and perf
	else if(key_hdr == MCPL_BLOCK_DONE_TEDGE) {
		nBlockDone++;

		// collect measurement for each processing
		perf.tTotal += payload;

		// check if all blocks have finished
		if(nBlockDone == blkInfo->maxBlock) {

			// collect total measurement
			perf.tTotal /= blkInfo->maxBlock;
			taskList.tPerf[taskList.cTaskPtr] = perf.tTotal;

			// then prepare for taskProcessingLoop again
			uchar ct = taskList.cTaskPtr + 1;
			taskList.cTask = taskList.tasks[ct];
			taskList.cTaskPtr = ct;
			spin1_schedule_callback(taskProcessingLoop, 0, 0, PRIORITY_PROCESSING);
		}
	}

	// the following MCPL_BCAST_SEND_RESULT will trigger a chain in it:
	else if(key==MCPL_BCAST_SEND_RESULT) {
		// if my block is enable, then proceed:
		if(blkInfo->maxBlock!=0)
			spin1_schedule_callback(sendResult, payload, NULL, PRIORITY_PROCESSING);
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
	// key MCPL_BCAST_START_PROC will be broadcasted by leadAp in root-node if
	// it receives the sdp message "End-of-Frame" via port SDP_PORT_FRAME_END.
	else if(key==MCPL_BCAST_START_PROC) {
		spin1_schedule_callback(triggerProcessing, payload, NULL, PRIORITY_PROCESSING);
	}
	else if(key==MCPL_BCAST_ALL_REPORT) {
		// MCPL_BCAST_ALL_REPORT will be broadcasted to all cores in the system
		// and the requesting info will be provided in the payload
		spin1_schedule_callback(give_report, payload, 0, PRIORITY_PROCESSING);
	}

	/*----------------------------------------------------------------------------*/
	/*----------------------------------------------------------------------------*/
	/*----------------------- new Send result mechanism --------------------------*/
	else if(key==MCPL_SEND_PIXELS_CMD) {
		spin1_schedule_callback(sendResultProcessCmd, payload, NULL, PRIORITY_PROCESSING);
	}
	else if(key==MCPL_SEND_PIXELS_DATA) {
		TERAKHIR (26 Sep jam 18:07) sampai disini...
	}

}

void hSDP(uint mBox, uint port)
{
	// Note: the variable msg might be released somewhere else,
	//       especially when delivering frame's channel
	sdp_msg_t *msg = (sdp_msg_t *)mBox;

#if (DEBUG_LEVEL>2)
	io_printf(IO_STD, "got sdp tag = 0x%x, srce_port = 0x%x, srce_addr = 0x%x, dest_port = 0x%x\n",
			  msg->tag, msg->srce_port, msg->srce_addr, msg->dest_port);
#endif

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
			io_printf(IO_STD, "[CONFIG] Got frame info...\n");
			// will only send wImg (in arg1.high) and hImg (in arg1.low)
			blkInfo->wImg = msg->arg1 >> 16;
			blkInfo->hImg = msg->arg1 & 0xFFFF;

			// then allocate image buffer in SDRAM
			allocateImgBuf();

			// send frame info to other nodes
			spin1_send_mc_packet(MCPL_BCAST_FRAME_INFO, msg->arg1, WITH_PAYLOAD);

			// then broadcast to all workers (including leadAp) to compute their workload
			spin1_send_mc_packet(MCPL_BCAST_GET_WLOAD, 0, WITH_PAYLOAD);

			// we also need to reset the dma token, so that it starts from LEAD_CORE again
			blkInfo->dmaToken_pxStore = LEAD_CORE;
		}
		else if(msg->cmd_rc == SDP_CMD_END_VIDEO) {





			// TODO:..... Clean memory
			releaseImgBuf();
			initImgBufs();


		}
	}

	/*--------------------------------------------------------------------------------------*/
	/*--------------------------------------------------------------------------------------*/
	/*-------------- New revision: several cores may receives frames directly --------------*/

	// NOTE: srce_addr TIDAK BISA DIPAKAI UNTUK pxSeq !!!!!
	// Karena srce_addr PASTI bernilai 0 jika dikirim dari host-PC

	/* Synopsys:
	 * Host will send 3 chunks of data (RGB) to one core at one time, and then
	 * send then next 3 chunks of data (RGB) to the next core. This way, each core
	 * should have enough time to do grayscaling, histogram counting, storing, and
	 * broadcasting.
	 * */

	else if(port==SDP_PORT_R_IMG_DATA) {
		pxBuffer.pxSeq = (msg->tag << 8) | msg->srce_port;
		pxBuffer.pxLen = msg->length - 8;
		sark_mem_cpy(rpxbuf, &msg->cmd_rc, pxBuffer.pxLen);	

		// Debugging 28.07.2016: how many pxSeq?
		//io_printf(IO_BUF, "Got rpx Seq = %d, Len = %d\n", pxBuffer.pxSeq, pxBuffer.pxLen);

		// NOTE: don't forward yet, the core is still receiving "fast" sdp packets
	}
	else if(port==SDP_PORT_G_IMG_DATA) {
		pxBuffer.pxSeq = (msg->tag << 8) | msg->srce_port;
		pxBuffer.pxLen = msg->length - 8;
		sark_mem_cpy(gpxbuf, &msg->cmd_rc, pxBuffer.pxLen);

		//io_printf(IO_BUF, "Got gpx Seq = %d, Len = %d\n", pxBuffer.pxSeq, pxBuffer.pxLen);
		// NOTE: don't forward yet, the core is still receiving "fast" sdp packets
	}
	else if(port==SDP_PORT_B_IMG_DATA) {
		pxBuffer.pxSeq = (msg->tag << 8) | msg->srce_port;
		pxBuffer.pxLen = msg->length - 8;
		sark_mem_cpy(bpxbuf, &msg->cmd_rc, pxBuffer.pxLen);

		//io_printf(IO_BUF, "Got bpx Seq = %d, Len = %d\n", pxBuffer.pxSeq, pxBuffer.pxLen);

		// process gray scalling and forward afterwards
		spin1_schedule_callback(processGrayScaling, 0, 0, PRIORITY_PROCESSING);
	}

	/*--------------------------------------------------------------------------------------*/
	/*--------------------------------------------------------------------------------------*/
	/*-------------- Host will send an empty message at port SDP_PORT_FRAME_END ------------*/
	/*-------------- to trigger spiNNaker to do the processing                  ------------*/
	/*-------------- This command should be sent to core<0,0,LEAD_CORE>         ------------*/
	else if(port==SDP_PORT_FRAME_END) {
		// at this point, the task list has already been built in the root-leadAp
		// then go to event-loop for task processing
		spin1_schedule_callback(taskProcessingLoop,0,0,PRIORITY_PROCESSING);
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

