#include "SpiNNVid.h"

// leadAp might use get_Nworkers() to count how many cores
// are in active state AND with ID of SpiNNVid. Once it knows,
// leadAp can use it to count the reply packet from workers.
uchar get_Nworkers()
{
	uchar i, nCores = sv->num_cpus, nApp = 0;
	for(i=0; i<nCores; i++) {
		if(sv->vcpu_base[i].app_id == SPINNVID_APP_ID) {
			if(sv->vcpu_base[i].cpu_state >= CPU_STATE_RUN &&
			   sv->vcpu_base[i].cpu_state < CPU_STATE_EXIT)
				nApp++;
		}
	}
	//io_printf(IO_STD, "Found %d available workers\n", nApp);
	return nApp;
}

// give_report might be executed by all cores or just leadAp
// if called from hSDP, reportType is the msg->seq
void give_report(uint reportType, uint target)
{

	char *dest;
	dest = target==0?IO_BUF:IO_STD;
#if(DEBUG_LEVEL > 0)
	// only for leadAp
	if(myCoreID == LEAD_CORE) {
		if(reportType==DEBUG_REPORT_WID) {
			io_printf(dest, "Total workers = %d:\n---------------------\n", workers.tAvailable);
			for(uint i=0; i<workers.tAvailable; i++)
				io_printf(IO_BUF, "wID-%d is core-%d\n", i, workers.wID[i]);
			io_printf(dest, "------------------------\n");
		}
		else if(reportType==DEBUG_REPORT_NWORKERS) {
			io_printf(dest, "Found %d active cores\n", get_Nworkers());
		}
		else if(reportType==DEBUG_REPORT_NET_CONFIG) {
			io_printf(dest, "\n--- Network configuration ---\n");
			io_printf(dest, "Node-ID  = %d\n", blkInfo->nodeBlockID);
			io_printf(dest, "MaxBlock = %d\n", blkInfo->maxBlock);
			io_printf(dest, "opType = %d\n", blkInfo->opType);
			io_printf(dest, "opFilter = %d\n", blkInfo->opFilter);
			io_printf(dest, "opSharpen = %d\n", blkInfo->opSharpen);
			io_printf(dest, "SDPdelFac = %d\n", sdpDelayFactorSpin);
			io_printf(dest, "-----------------------------\n");
		}
		else if(reportType==DEBUG_REPORT_PLL_INFO) {
			// send MCPL to all profilers
			if(sv->p2p_addr==0)
				spin1_send_mc_packet(MCPL_TO_ALL_PROFILER, PROF_MSG_PLL_INFO, WITH_PAYLOAD);
		}
		else if(reportType==DEBUG_REPORT_IMGBUFS) {
			if(workers.active==TRUE) {
				io_printf(IO_BUF, "DTCM Image Buffer allocation:\n---------------------------------\n");
				io_printf(IO_BUF, "dtcmImgBuf = 0x%x\n", dtcmImgBuf);
				io_printf(IO_BUF, "resImgBuf  = 0x%x\n", resImgBuf);
				io_printf(IO_BUF, "---------------------------------\n\n");
				debugMsg.cmd_rc = DEBUG_REPORT_IMGBUF_IN;
				debugMsg.seq = blkInfo->nodeBlockID;
				debugMsg.arg1 = (uint)blkInfo->imgRIn;
				debugMsg.arg2 = (uint)blkInfo->imgGIn;
				debugMsg.arg3 = (uint)blkInfo->imgBIn;
				spin1_send_sdp_msg(&debugMsg, 10);
				spin1_delay_us(10);
				debugMsg.cmd_rc = DEBUG_REPORT_IMGBUF_OUT;
				debugMsg.seq = blkInfo->nodeBlockID;
				debugMsg.arg1 = (uint)blkInfo->imgOut1;
				//debugMsg.arg2 = (uint)blkInfo->imgOut2;
				//debugMsg.arg3 = (uint)blkInfo->imgOut3;
				spin1_send_sdp_msg(&debugMsg, 10);
				spin1_delay_us(blkInfo->nodeBlockID*1000);
			}
		}
		else if(reportType==DEBUG_REPORT_TASKLIST) {
			if(sv->p2p_addr==0) {
				io_printf(IO_STD, "Task list:\n");
				char taskStr[15];
				for(uchar i=0; i<taskList.nTasks; i++) {
					getTaskName(taskList.tasks[i], taskStr);
					/*
					switch(taskList.tasks[i]){
					case PROC_FILTERING:
						io_printf(taskStr, "FILTERING"); break;
					case PROC_SHARPENING:
						io_printf(taskStr, "SHARPENING"); break;
					case PROC_EDGING_DVS:
						io_printf(taskStr, "EDGING_DVS"); break;
					case PROC_SEND_RESULT:
						io_printf(taskStr, "RESULTING"); break;
					}
					*/
					io_printf(IO_STD, "Task-%d : %s\n", i, taskStr);
				}
			}
		}
		else if(reportType==DEBUG_REPORT_EDGE_DONE) {
			if(sv->p2p_addr==0) dest=IO_STD; else dest=IO_BUF;
			io_printf(dest, "\nworkers.tRunning = %d\n", workers.tRunning);
			io_printf(dest, "nWorkerDone = %d\n\n", nWorkerDone);
		}
	}
	// for all cores
	if(reportType==DEBUG_REPORT_MYWID) {
		io_printf(dest, "My wID = %d\n", workers.subBlockID);
	}
	else if(reportType==DEBUG_REPORT_PERF){
		io_printf(dest, "[wID-%d] perf.tEdge = %u\n",
				  workers.subBlockID, perf.tCore);
	}
	else if(reportType==DEBUG_REPORT_BLKINFO) {
		if(workers.active==TRUE) {
			//io_printf(dest, "opType=%d, opFilter=%d, opSharp=%d\n",
			//		  blkInfo->opType, blkInfo->opFilter, blkInfo->opSharpen);
			io_printf(dest, "Node block info\n---------------------\n");
			//io_printf(dest, "@blkInfo = 0x%x\n", blkInfo);
			io_printf(dest, "Node-ID  = %d\n", blkInfo->nodeBlockID);
			io_printf(dest, "MaxBlock = %d\n", blkInfo->maxBlock);
			io_printf(dest, "Nworkers = %d\n", blkInfo->Nworkers);
			io_printf(dest, "my wID   = %d\n", workers.subBlockID);
			switch(blkInfo->opType){
			case 0: io_printf(dest, "OpType   = None\n"); break;
			case 1: io_printf(dest, "OpType   = SOBEL\n"); break;
			case 2: io_printf(dest, "OpType   = LAPLACE\n"); break;
			case 3: io_printf(dest, "OpType   = DVS\n"); break;
			}
			switch(blkInfo->opFilter){
			case 0: io_printf(dest, "OpFilter = no FILTERING\n"); break;
			case 1: io_printf(dest, "OpFilter = with FILTERING\n"); break;
			}
			switch(blkInfo->opSharpen){
			case 0: io_printf(dest, "OpSharp  = no SHARPENING\n"); break;
			case 1: io_printf(dest, "OpSharp  = with SHARPENING\n"); break;
			}

			io_printf(dest, "wFrame   = %d\n", blkInfo->wImg);
			io_printf(dest, "hFrame   = %d\n", blkInfo->hImg);
			io_printf(dest, "------------------------\n", workers.tAvailable);
		}
		else {
			io_printf(dest, "[wID-%d] Nothing to report yet (maybe I'm disabled?)\n",
					  workers.subBlockID);
		}
	}
	else if(reportType==DEBUG_REPORT_WLOAD) {
		io_printf(dest, "nLinesPerBlock = %d, tAvailable = %d, tRunning = %d\n",
				  workers.nLinesPerBlock, workers.tAvailable, workers.tRunning);
		if(workers.active==TRUE) {
			io_printf(dest, "[wID-%d] blkID-%d, sp = %d, ep = %d\n",
					  workers.subBlockID, blkInfo->nodeBlockID, workers.startLine,
					  workers.endLine);
			debugMsg.cmd_rc = DEBUG_REPORT_WLOAD;
			debugMsg.arg1 = (blkInfo->maxBlock << 8) + blkInfo->nodeBlockID;
			// seq: how many workers in the block and what's current worker ID
			debugMsg.arg2 = (blkInfo->Nworkers << 8) + workers.subBlockID;
			debugMsg.arg3 = (workers.startLine << 16) + workers.endLine;
			spin1_delay_us((blkInfo->nodeBlockID*17+workers.subBlockID)*100);

			spin1_send_sdp_msg(&debugMsg, 10);
		} else {
			io_printf(dest, "[wID-%d] blkID-%d, sp = %d, ep = %d: But I'm disabled!!!\n",
					  workers.subBlockID, blkInfo->nodeBlockID, workers.startLine,
					  workers.endLine);
		}
	}
	else if(reportType==DEBUG_REPORT_FRAMEINFO) {
		if(workers.active==TRUE) {
			io_printf(dest, "[wID-%d] blkInfo->wImg = %d, blkInfo->hImg = %d\n",
					  workers.subBlockID, blkInfo->wImg, blkInfo->hImg);
		} else {
			io_printf(dest, "[wID-%d] blkInfo->wImg = %d, blkInfo->hImg = %d:",
					  workers.subBlockID, blkInfo->wImg, blkInfo->hImg);
			io_printf(dest, "But I'm disabled!!!\n");
		}

	}
	else if(reportType==DEBUG_REPORT_HISTPROP) {
		io_printf(dest, "Histogram propagation clock period = %u\n", perf.tCore);
	}
#endif
}

/*
void seePxBuffer(char *stream)
{
	if(sv->p2p_addr != 0) return;
	ushort i;
	io_printf(stream, "Content of rba at pxSeq-%d:\n", pxBuffer.pxSeq);
	for(i=0; i<pxBuffer.pxLen; i++) {
		//io_printf(stream, "%0x ", pxBuffer.rpxbuf[i]);
		io_printf(stream, "%0x ", rpxbuf[i]);
	}
	io_printf(stream, "\n---------------------------------\n");
	io_printf(stream, "Content of gba at pxSeq-%d:\n", pxBuffer.pxSeq);
	for(i=0; i<pxBuffer.pxLen; i++) {
		//io_printf(stream, "%0x ", pxBuffer.gpxbuf[i]);
		io_printf(stream, "%0x ", gpxbuf[i]);
	}
	io_printf(stream, "\n---------------------------------\n");
	io_printf(stream, "Content of bba at pxSeq-%d:\n", pxBuffer.pxSeq);
	for(i=0; i<pxBuffer.pxLen; i++) {
		//io_printf(stream, "%0x ", pxBuffer.bpxbuf[i]);
		io_printf(stream, "%0x ", bpxbuf[i]);
	}
	io_printf(stream, "\n---------------------------------\n");
	io_printf(stream, "Content of yba at pxSeq-%d:\n", pxBuffer.pxSeq);
	for(i=0; i<pxBuffer.pxLen; i++) {
		//io_printf(stream, "%0x ", pxBuffer.ypxbuf[i]);
		io_printf(stream, "%0x ", ypxbuf[i]);
	}
	io_printf(stream, "\n---------------------------------\n");

}

void peekPxBufferInSDRAM(char *stream)
{
	//if(sv->p2p_addr != 0) return;
	uchar px;
	ushort i;
	uchar *offset;
	io_printf(stream, "Content of rba at 0x%x:\n",
			  blkInfo->imgRIn+pxBuffer.pxSeq*DEF_PXLEN_IN_CHUNK);
	for(i=0; i<pxBuffer.pxLen; i++) {
		offset = blkInfo->imgRIn + pxBuffer.pxSeq*DEF_PXLEN_IN_CHUNK + i;
		sark_mem_cpy(&px, offset, 1);
		io_printf(stream, "%0x ", px);
	}
	io_printf(stream, "\n---------------------------------\n");
	io_printf(stream, "Content of gba at 0x%x:\n",
			  blkInfo->imgGIn+pxBuffer.pxSeq*DEF_PXLEN_IN_CHUNK);
	for(i=0; i<pxBuffer.pxLen; i++) {
		offset = blkInfo->imgGIn + pxBuffer.pxSeq*DEF_PXLEN_IN_CHUNK + i;
		sark_mem_cpy(&px, offset, 1);
		io_printf(stream, "%0x ", px);
	}
	io_printf(stream, "\n---------------------------------\n");
	io_printf(stream, "Content of bba at 0x%x:\n",
			  blkInfo->imgBIn+pxBuffer.pxSeq*DEF_PXLEN_IN_CHUNK);
	for(i=0; i<pxBuffer.pxLen; i++) {
		offset = blkInfo->imgBIn + pxBuffer.pxSeq*DEF_PXLEN_IN_CHUNK + i;
		sark_mem_cpy(&px, offset, 1);
		io_printf(stream, "%0x ", px);
	}
	io_printf(stream, "\n---------------------------------\n");
	io_printf(stream, "Content of yba at 0x%x:\n",
			  blkInfo->imgOut1+pxBuffer.pxSeq*DEF_PXLEN_IN_CHUNK);
	for(i=0; i<pxBuffer.pxLen; i++) {
		offset = blkInfo->imgOut1 + pxBuffer.pxSeq*DEF_PXLEN_IN_CHUNK + i;
		sark_mem_cpy(&px, offset, 1);
		io_printf(stream, "%0x ", px);
	}
	io_printf(stream, "\n---------------------------------\n");

}
*/

inline REAL roundr(REAL inVal)
{
	uint base = (uint)inVal;
	uint upper = base + 1;
	REAL conver = inVal + REAL_CONST(0.5);
	if((uint)conver == base)
		return (REAL)base;
	else
		return (REAL)upper;
}

volatile uint giveDelay(uint delVal)
{
  volatile uint dummy = delVal;
  volatile uint step = 0;
  while(step < delVal) {
    dummy += (2 * step);
    step++;
  }
  return dummy;
}


void getChipXYfromID(ushort id, ushort *X, ushort *Y)
{
    for(ushort i=0; i<blkInfo->maxBlock; i++) {
        if(chips[i].id = id) {
            *X = chips[i].x;
            *Y = chips[i].y;
            break;
        }
    }
}


/*----------------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------------*/
/*---------------------------- Deprecated functions --------------------------------*/
// get default number of block for different board
uchar get_def_Nblocks()
{
	uchar N;
#if(USING_SPIN==3)
	N = 4;
#else
	N = 48;
#endif
	return N;
}

// get_block_id() will use sv->p2p_addr to determine the
// default node-ID for the current chip
uchar get_block_id()
{
	uchar N;

#if(USING_SPIN==3)
	N = CHIP_X(sv->p2p_addr) * 2 + CHIP_Y(sv->p2p_addr);
#else
	uchar x[48] = {0,1,2,3,4,
				   0,1,2,3,4,5,
				   0,1,2,3,4,5,6,
				   0,1,2,3,4,5,6,7,
					 1,2,3,4,5,6,7,
					   2,3,4,5,6,7,
						 3,4,5,6,7,
						   4,5,6,7};
	uchar y[48] = {0,0,0,0,0,
				   1,1,1,1,1,1,
				   2,2,2,2,2,2,2,
				   3,3,3,3,3,3,3,3,
					 4,4,4,4,4,4,4,
					   5,5,5,5,5,5,
						 6,6,6,6,6,
						   7,7,7,7};

	for(uchar i=0; i<48; i++) {
		if((CHIP_X(sv->p2p_addr)==x[i]) && (CHIP_Y(sv->p2p_addr)==y[i])) {
			N = i;
			break;
		}
	}
#endif
	return N;
}

void getTaskName(proc_t proc, char strBuf[])
{
	switch(proc){
	case PROC_FILTERING: io_printf(strBuf, "FILTERING"); break;
	case PROC_SHARPENING: io_printf(strBuf, "SHARPENING"); break;
	case PROC_EDGING_DVS: io_printf(strBuf, "EDGING_DVS"); break;
	case PROC_SEND_RESULT: io_printf(strBuf, "RESULTING"); break;
	}
}
