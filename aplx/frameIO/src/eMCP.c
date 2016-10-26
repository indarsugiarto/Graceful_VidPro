#include "frameIO.h"

/*------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------*/
/*------------------- Sub functions called by main handler ---------------------*/


void hMC(uint key, uint None)
{
	uint key_hdr = key & 0xFFFF0000;
	uint key_arg = key & 0xFFFF;
	if(key_hdr==MCPL_FRAMEIO_FWD_WID)
		pxFwdr_wID = key_arg;
}

/* Rule: MCPL always contains key_hdr and key_arg
 * */
void hMCPL(uint key, uint pload)
{
	uint key_hdr = key & 0xFFFF0000;
	uint key_arg = key & 0xFFFF;

	// if frame size is broadcasted by LEAD_CORE
	if(key_hdr==MCPL_FRAMEIO_SZFRAME) {
		// at this point, nCorePerPipe must already be valid
		// either pre-defined, or detected (see main.c)
		spin1_schedule_callback(computeWload, pload, 0, PRIORITY_PROCESSING);
	}
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

void hMC_SpiNNVid(uint key, uint None)
{
	uint key_hdr = 0xFFFF0000 & key;
	uint key_arg = 0xFFFF & key;
	if((key_hdr & 0xFF000000) == MCPL_SEND_PIXELS_BLOCK_CORES_NEXT) {
		flag_SendResultCont = TRUE;
	}
	else if((key_hdr & 0xFF000000) == MCPL_SEND_PIXELS_BLOCK_CORES_INIT) {
		sendResultInfo.nReceived_MCPL_SEND_PIXELS = 0;
		sendResultInfo.pxBufPtr = resImgBuf;
		sendResultInfo.cl = key_arg;
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
		if(myCoreID==LEAD_CORE) return;
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
	// MCPL_BCAST_GET_WLOAD is broadcasted when new frame info arrives
	else if(key==MCPL_BCAST_GET_WLOAD) {
		spin1_schedule_callback(computeWLoad, 0, 0, PRIORITY_PROCESSING);
	}

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
		blkInfo->opType = (payload >> 4) & 0xF;
		blkInfo->opFilter = (payload >> 2) & 0x3;
		blkInfo->opSharpen = payload & 0x3;
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
		blkInfo->maxBlock = 0;
		blkInfo->nodeBlockID = 0xFF;
	}
	else if(key==MCPL_BCAST_NET_DISCOVERY) {
		// without delay, there might be packets that not captured by the root-leadAp
		// although those packets are not dropped by the router
		sark_delay_us(myChipID);
		spin1_send_mc_packet(MCPL_BCAST_NET_REPLY, sv->p2p_addr, WITH_PAYLOAD);
	}
	else if(key==MCPL_BCAST_NET_REPLY) {
		aliveNodeAddr[nChipAlive] = payload;
		nChipAlive++;
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


		// so, what if we ignore it?
		return;	// so that we can proceed with the next task

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
			// give a short delay to prevent packet missing (not dropped by router):
			sark_delay_us(myChipID);
			uint key = MCPL_BLOCK_DONE_TEDGE | blkInfo->nodeBlockID;
			spin1_send_mc_packet(key, perf.tNode, WITH_PAYLOAD);

			// and tell profiler that processing is done for a node
			// spin1_send_mc_packet(MCPL_TO_OWN_PROFILER, PROF_MSG_PROC_END, WITH_PAYLOAD);
			// no need, we just need to wait from root-leadAp
		}
	}

	// MCPL_FILT_DONE is sent by every core to leadAp in the node
	else if(key==MCPL_FILT_DONE) {

		// the same, what if we ignore it?
		return;

		nWorkerDone++;
		perf.tNode += payload;
		if(nWorkerDone==workers.tRunning) {
			perf.tNode /= nWorkerDone;

			// send notification to root-leadAp
			sark_delay_us(myChipID);
			uint key = MCPL_BLOCK_DONE_TFILT | blkInfo->nodeBlockID;
			spin1_send_mc_packet(key, perf.tNode, WITH_PAYLOAD);
		}
	}

	// MCPL_BLOCK_DONE_TEDGE is sent by each node to <0,0,LEAD_CORE>,
	// which contains node-ID and perf
	else if(key_hdr == MCPL_BLOCK_DONE_TEDGE) {

		// what if we ignore it? we don't need again
		return;	// below this is for measurement per block

		nBlockDone++;

		// collect measurement for each processing
		perf.tTotal += payload;

		// check if all blocks have finished
		if(nBlockDone == blkInfo->maxBlock) {

			// collect total measurement
			perf.tTotal /= blkInfo->maxBlock;
			taskList.tPerf[taskList.cTaskPtr] = perf.tTotal;

			// then prepare for taskProcessingLoop again
			notifyTaskDone();
		}
	}

	// MCPL_BLOCK_DONE_TFILT is sent by each node to root-leadAp
	else if(key_hdr == MCPL_BLOCK_DONE_TFILT) {

		// the same here, what if we ignore it? no need for measurement...
		return;

		// collect measurement for each processing
		nBlockDone++;
		perf.tTotal += payload;

		// check if all blocks have finished
		if(nBlockDone == blkInfo->maxBlock) {

			// collect total measurement
			perf.tTotal /= blkInfo->maxBlock;
			taskList.tPerf[taskList.cTaskPtr] = perf.tTotal;

			// then prepare for taskProcessingLoop again
			notifyTaskDone();
		}
	}

	// MCPL_RECV_END_OF_FRAME is sent by some cores to LEAD_CORE
	// as a means for fault tolerance regarding SDP_PORT_FRAME_END
	else if(key_hdr == MCPL_RECV_END_OF_FRAME) {
		// first, check if taskList.EOF_flag is the same as the payload
		if(taskList.EOF_flag != (int)payload) {
			// yes, take it and schedule taskProcessingLoop
#if(DEBUG_LEVEL>0)
			io_printf(IO_STD, "[EOF_CMD] Got EOF from core-%d...\n", key_arg);
#endif
			taskList.EOF_flag = (int)payload;
			spin1_schedule_callback(taskProcessingLoop,payload,0,PRIORITY_PROCESSING);
		}
	}

	// LEAD_CORE will broadcast MCPL_IGNORE_END_OF_FRAME when it enters the Loop
	else if(key==MCPL_IGNORE_END_OF_FRAME) {
		if(myCoreID==LEAD_CORE) return;
		taskList.EOF_flag = (int)payload;	// node, each core has taskList variable
											// but not fully used
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
	//see MCPL_BCAST_REPORT_HIST in hMC_SpiNNVid
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
	/* This is the sendResult using buffering mechanism */
	// MCPL_SEND_PIXELS_BLOCK is sent by root-leadAp to other leadAps
	else if(key_hdr == MCPL_SEND_PIXELS_BLOCK && payload == blkInfo->nodeBlockID) {
		nWorkerDone = 0;
		// then notify the workers:
		spin1_send_mc_packet(MCPL_SEND_PIXELS_BLOCK_CORES, 0, WITH_PAYLOAD);
	}
	// if the core receives pixels data from other nodes with the same coreID
	else if((key_hdr & 0xFF000000) == MCPL_SEND_PIXELS_BLOCK_CORES_DATA) {
#if(DEBUG_LEVEL>2)
		io_printf(IO_BUF, "For line-%d got key-0x%x, pl-0x%x\n", sendResultInfo.cl, key, payload);
#endif
		// first, copy the first 2 pixels in the key part
		ushort firstPart = key_arg;
		sark_mem_cpy(sendResultInfo.pxBufPtr, (void *)&firstPart, 2);
		sendResultInfo.pxBufPtr +=2;
		// then, copy the next 4 pixels in the payload
		sark_mem_cpy(sendResultInfo.pxBufPtr, (void *)&payload, 4);
		sendResultInfo.pxBufPtr += 4;
		sendResultInfo.nReceived_MCPL_SEND_PIXELS += 6;

		if(sendResultInfo.nReceived_MCPL_SEND_PIXELS >= workers.wImg)
#if(DEBUG_LEVEL>2)
			io_printf(IO_BUF, "Got the line!\n");
#endif
			spin1_schedule_callback(worker_recv_result, 0, 0, PRIORITY_PROCESSING);
	}
	// MCPL_SEND_PIXELS_BLOCK_CORES is  sent from leadAp to its workers
	else if(key_hdr == MCPL_SEND_PIXELS_BLOCK_CORES) {
		spin1_schedule_callback(worker_send_result, 0, 0, PRIORITY_PROCESSING);
	}
	// MCPL_SEND_PIXELS_BLOCK_CORES_DONEs are sent from workers to leadAp
	else if(key_hdr == MCPL_SEND_PIXELS_BLOCK_CORES_DONE) {
		nWorkerDone++;
		// Note: not all workers might running, especially if nLines << nWorker
		if(nWorkerDone==workers.tRunning) {
#if(DEBUG_LEVEL>2)
			io_printf(IO_STD, "Block-%d done!\n", blkInfo->nodeBlockID);
#endif
			spin1_send_mc_packet(MCPL_SEND_PIXELS_BLOCK_DONE, 0, WITH_PAYLOAD);
		}
	}
	// if a block has sent all its part, it notifies the root-leadAp, which
	// restart the sendResultChain again
	else if(key_hdr == MCPL_SEND_PIXELS_BLOCK_DONE) {
		nBlockDone++;
		spin1_schedule_callback(sendResultChain, nBlockDone, 0, PRIORITY_PROCESSING);
	}
	/* We disable MCPL_SEND_PIXELS_BLOCK_PREP in this wBufferingOptim
	// MCPL_SEND_PIXELS_BLOCK_PREP is sent by root-leadAp to prepare its workers
	else if(key_hdr == MCPL_SEND_PIXELS_BLOCK_PREP) {
		sendResultInfo.nReceived_MCPL_SEND_PIXELS = 0;
		sendResultInfo.pxBufPtr = resImgBuf;
		// at this point, 4-byte aligned resImgBuf should already exist
	}
	*/
/*
#if(DEBUG_LEVEL > 1)
	else {
		io_printf(IO_BUF, "Got key-0x%x, pay-0x%x\n", key, payload);
	}
#endif
*/
}


void hTimer(uint tick, uint Unused)
{
	if(tick==1) {
		// how many workers are there?
		spin1_schedule_callback(give_report, DEBUG_REPORT_NWORKERS, 0, PRIORITY_PROCESSING);
	}
#if(DEBUG_LEVEL>2)
	else if(tick==2) {
		if(sv->p2p_addr==0) {
			//how many chips are there?
			aliveNodeAddr[0] = 0;	// myself
			nChipAlive = 1;
			spin1_send_mc_packet(MCPL_BCAST_NET_DISCOVERY, 0, WITH_PAYLOAD);
		}
	}
	else if(tick==3) {
		if(sv->p2p_addr==0) {
			io_printf(IO_STD, "[SpiNNVid] Available node = %d\n", nChipAlive);
			for(ushort i=0; i<nChipAlive; i++) {
				io_printf(IO_BUF, "Node-%d = <%d,%d>\n", i,
						  CHIP_X(aliveNodeAddr[i]), CHIP_Y(aliveNodeAddr[i]));
			}
			io_printf(IO_STD, "[SpiNNVid] We are ready!\n");
		}
	}
#endif
}

