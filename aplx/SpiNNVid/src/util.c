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
			io_printf(dest, "------------------------\n", workers.tAvailable);
		}
		else if(reportType==DEBUG_REPORT_NWORKERS) {
			io_printf(dest, "Found %d active cores\n", get_Nworkers());
		}
		else if(reportType==DEBUG_REPORT_BLKINFO) {
			io_printf(dest, "Node block info\n---------------------\n");
			io_printf(dest, "Node-ID  = %d\n", blkInfo->nodeBlockID);
			io_printf(dest, "MaxBlock = %d\n", blkInfo->maxBlock);
			io_printf(dest, "Nworkers = %d\n", blkInfo->Nworkers);
			if(blkInfo->opType==0) {
				if(blkInfo->opFilter==0)
					io_printf(dest, "OpType   = SOBEL no FILTER\n");
				else
					io_printf(dest, "OpType   = SOBEL with FILTER\n");
			} else {
				if(blkInfo->opFilter==0)
					io_printf(dest, "OpType   = LAPLACE no FILTER\n");
				else
					io_printf(dest, "OpType   = LAPLACE with FILTER\n");
			}
			io_printf(dest, "wFrame   = %d\n", blkInfo->wImg);
			io_printf(dest, "hFrame   = %d\n", blkInfo->hImg);
			io_printf(dest, "------------------------\n", workers.tAvailable);
		}
	}
	if(reportType==DEBUG_REPORT_MYWID) {
		io_printf(dest, "My wID = %d\n", workers.subBlockID);
	}
	else if(reportType==DEBUG_REPORT_WLOAD) {
		// send the workload via tag-3
		// let's print the resulting workload
		io_printf(IO_BUF, "blkID-%d, wID-%d, sp = %d, ep = %d\n",
				  blkInfo->nodeBlockID, workers.subBlockID, workers.startLine, workers.endLine);
		// printWLoad();
		// cmd_rc: how many blocks in the network and what's current blockID
		debugMsg.cmd_rc = (blkInfo->maxBlock << 8) + blkInfo->nodeBlockID;
		// seq: how many workers in the block and what's current worker ID
		debugMsg.seq = (blkInfo->Nworkers << 8) + workers.subBlockID;
		debugMsg.arg1 = (workers.startLine << 16) + workers.endLine;
		debugMsg.arg2 = (uint)workers.imgRIn;	// not important, just for check
		debugMsg.arg3 = (uint)workers.imgOut1;	// not important
		spin1_delay_us((blkInfo->nodeBlockID*17+workers.subBlockID)*100);

		spin1_send_sdp_msg(&debugMsg, 10);
	}
#endif
}
