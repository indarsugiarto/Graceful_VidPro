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



// give_report might be executed by all cores or just leadAp
void give_report(uint reportType, uint target)
{

	char *dest;
	dest = target==0?IO_BUF:IO_STD;
#if(DEBUG_LEVEL > 0)
	// only for leadAp
	if(leadAp) {
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
			io_printf(dest, "-----------------------------\n");
		}
	}
	// for all cores
	if(reportType==DEBUG_REPORT_MYWID) {
		io_printf(dest, "My wID = %d\n", workers.subBlockID);
	}
	else if(reportType==DEBUG_REPORT_PERF){
		io_printf(dest, "[wID-%d] perf.tEdge = %u\n",
				  workers.subBlockID, perf.tEdge);
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
			case 0: io_printf(dest, "OpType   = SOBEL\n"); break;
			case 1: io_printf(dest, "OpType   = LAPLACE\n"); break;
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
		if(workers.active==TRUE) {
			io_printf(dest, "[wID-%d] blkID-%d, sp = %d, ep = %d\n",
					  workers.subBlockID, blkInfo->nodeBlockID, workers.startLine,
					  workers.endLine);
			debugMsg.cmd_rc = (blkInfo->maxBlock << 8) + blkInfo->nodeBlockID;
			// seq: how many workers in the block and what's current worker ID
			debugMsg.seq = (blkInfo->Nworkers << 8) + workers.subBlockID;
			debugMsg.arg1 = (workers.startLine << 16) + workers.endLine;
			debugMsg.arg2 = (uint)workers.imgRIn;	// not important, just for check
			debugMsg.arg3 = (uint)workers.imgOut1;	// not important
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
#endif
}

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

	/*
	ushort i;
	io_printf(IO_BUF, "Content of rba at pxSeq-%d:\n", pxBuffer.pxSeq);
	for(i=0; i<pxBuffer.pxLen; i++) {
		io_printf(IO_BUF, "%k ", (REAL)pxBuffer.rpxbuf[i]);
	}

	ushort c = pxBuffer.rpxbuf[0];
	//c = 255;
	REAL r = (REAL)pxBuffer.rpxbuf[0];
	io_printf(IO_BUF, "\n---------------------------------\n");
	io_printf(IO_BUF, "rpxBuf[0] = %u\n",pxBuffer.rpxbuf[0]);
	io_printf(IO_BUF, "(REAL)255 = %k\n",(REAL)255);
	io_printf(IO_BUF, "(REAL)rpxBuf[0] = %k\n",(REAL)pxBuffer.rpxbuf[0]);
	io_printf(IO_BUF, "c = %d, (REAL)c = %k\n",c, (REAL)c);
	io_printf(IO_BUF, "r = %k\n",r);
	*/
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


inline REAL roundr(REAL inVal)
{
	uint base = (uint)inVal;
	uint upper = base + 1;
	REAL conver = inVal+REAL_CONST(0.5);
	if((uint)conver == base)
		return (REAL)base;
	else
		return (REAL)upper;
}

volatile uint giveDelay(uint delVal)
{
  volatile uint dummy = delVal;
  uint step = 0;
  while(step < delVal) {
    dummy += (2 * step);
    step++;
  }
  return dummy;
}
