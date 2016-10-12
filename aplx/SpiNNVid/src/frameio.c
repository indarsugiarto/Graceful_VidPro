/* This file will focus on sending (and receiving) images.
 * */

#include "SpiNNVid.h"

/*-------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------*/
/*------------------------- Sending results to Targets --------------------------*/


/* ------------------------------- sendResult()-----------------------------------
 * SYNOPSIS:
 * sendResult() is the main point to send the result to the target. It will be
 * executed ONLY by leadAp in the root-node.
 *
 * It is scheduled by triggerProcessing() (in process.c) with payload "0".
 * Normally, it it the last task before taskList got cleared (in this case, the
 * taskList will be cleared in sendResultChain() by calling notifyTaskDone().
 *
 * The sendResult() will trigger the chain mechanism by first scheduling
 * sendResultChain(). The sendResultChain() will be re-scheduled everytime
 * until all blocks have sent their part.
 *
 * In this version, the root-node will collect the pixel from other nodes, assemble
 * the sdp, and send to the target (host-pc or fpga).
 *
 * There are two similar subroutine sendResultToTarget and sendResultToTargetFromRoot:
 * - sendResultToTargetFromRoot is dedicated for the root-node and is executed before:
 * - sendResultToTarget is for other nodes
 * Those two subroutines are modified according to the value of DESTINATION
 *
 *------------------------------------------------------------------------------*/
void sendResult(uint unused, uint arg1)
{
	// as the root, we have the first chance to send the result
#if(DEBUG_LEVEL>0)
	io_printf(IO_STD, "sendResultToTargetFromRoot()\n");
#endif

#if(DESTINATION==DEST_HOST)
	sendResultToTargetFromRoot();
	nBlockDone = 1;		// the root part is done
#else
	// experiment: just send from one node and see the FR throughput!
	sendResultToTargetFromRoot();
	nBlockDone = blkInfo->maxBlock;

#endif

	// then the remaining nodes should execute sendResultToTarget()
#if(DEBUG_LEVEL>0)
	io_printf(IO_STD, "sendResultToTarget()\n");
#endif

	// then trigger the chain
	// NOTE: must event-based
	//       also, at this point, resultMsg.srce_port is set!

	sendResultInfo.blockToSend = 1;
	spin1_schedule_callback(sendResultChain, 1, 0, PRIORITY_PROCESSING);
}


/* sendResultChain() is the second after the main sendResult()
 * This is to iterate the block other than the root-node.
 *
 * */
void sendResultChain(uint nextBlock, uint unused)
{

	if(nBlockDone==blkInfo->maxBlock) {
#if(DEBUG_LEVEL>1)
		io_printf(IO_STD, "Notify host and set taskList...\n");
#endif
		notifyDestDone(0,0);
		notifyTaskDone();
	}
	else {
#if(DEBUG_LEVEL>1)
		io_printf(IO_STD, "sendResultChain for block-%d\n", nextBlock);
#endif
		sendResultInfo.nReceived_MCPL_SEND_PIXELS = 0;
		sendResultInfo.pxBufPtr = (uchar *)&resultMsg.cmd_rc;
		spin1_send_mc_packet(MCPL_SEND_PIXELS_CMD, nextBlock, WITH_PAYLOAD);
	}

	/* deprecated method: using nextLine parameter...
	//io_printf(IO_STD, "Lanjut chain-%d\n", nextLine); sark_delay_us(1000);
	// end of sending result? release the buffer
	if(nextLine==workers.hImg) {
		//sark_free(sendResultInfo.pxBuf);	// it is dealed together will all other bufs

		// then notify host that we've done with sending the result
		spin1_schedule_callback(notifyDestDone,0,0,PRIORITY_PROCESSING);

		// then go back to task processing loop
		notifyTaskDone();
	}
	else {
		// prepare the buffer (reset its pointer)
		sendResultInfo.pxBufPtr = sendResultInfo.pxBuf;

		// debugging sampai 220, lihat apa ada MCPL stuck... TIDAK
		// tapi kenapa sampai 225 ada dumped MCPL... ????
		//if(nextLine < 230) {
		// prepare the pixel-chunk counter:
		sendResultInfo.nReceived_MCPL_SEND_PIXELS = 0;

		//io_printf(IO_STD, "Will bcast MCPL_SEND_PIXELS_CMD\n");
		spin1_send_mc_packet(MCPL_SEND_PIXELS_CMD, nextLine, WITH_PAYLOAD);
		//}
	}
	*/
}


// we cannot use workers.imgROut, because workers.imgROut differs from core to core
// use workers.blkImgROut instead!
// nodeID is the next node that is supposed to send the result
// NOTE: in this version, we'll send the gray image. Refer to pA for rgb version!
// The parameter arg1 is used for delay when sending the output to FPGA.
// Hence, in normal operation (send the result to host-PC), arg1 should be 0.
#if(DESTINATION==DEST_HOST)
void sendResultToTargetFromRoot()
{

	/*
	Synopsis:
	1. for sending pixel, use srce_port as chunk index
			hence, the maximum chunk = 253, which reflect the maximum 253*272=68816 pixels
			in one line (thus, we can expect the maximum image size is 68816*whatever).
	2. for notifying host, use srce_port 0xFE
	   we cannot use 0xFF, because 0xFF is special for ETH

	// Note: dtcmImgBuf has been allocated in computeWLoad(),
	// but it can be 3 or 5 times the image width.
	// For fetching only one line, use resImgBuf instead

	Idea 4 speed-up: fetch the data from sdram using DMA, BUT put directly in the sdp buffer.
	*/

	uchar *imgOut;
	imgOut  = (uchar *)getSdramResultAddr();

	// debugging: OK
	// io_printf(IO_BUF, "imgOut = 0x%x\n", imgOut);

	uint dmatag = DMA_FETCH_IMG_TAG | (myCoreID << 16);


	uchar *ptrBuf = (uchar *)&resultMsg.cmd_rc;

	uchar chunkID = 0;
	uint rem, sz;

	//experiment: berapa cepat jika semua dikirim dari node-1 --> hasil: 8.6 & 5.6 MBps
	//rem = workers.hImg * workers.wImg;	// kirim semuanya!

	//for normal run, just do my part:
	rem = (workers.blkEnd - workers.blkStart + 1) * workers.wImg;
	// since we don't use srce_port anymore, we have to fill it with something...
	resultMsg.srce_port = (SDP_PORT_FRAME_END << 5) | myCoreID;
	do {
		sz = rem > 272 ? 272 : rem;
		dmaImgFromSDRAMdone = 0;	// will be altered in hDMA
		do {
			//dmaTID = spin1_dma_transfer(dmatag, (void *)imgOut,
			//							(void *)resImgBuf, DMA_READ, workers.wImg);
			dmaTID = spin1_dma_transfer(dmatag, imgOut,
										ptrBuf, DMA_READ, sz);
		} while(dmaTID==0);

		// wait until dma is completed
		while(dmaImgFromSDRAMdone==0) {
		}

		// then sequentially copy & send via sdp. We use srce_addr and srce_port as well:
		sendImgChunkViaSDP(sz, 0);

		chunkID++;

		// move to the next address
		imgOut += sz;
		rem -= sz;

	} while(rem > 0);
	//}
}

#else

void sendImgChunkViaFR(ushort y, uchar * pxbuf)
{
	uint key;
	uchar px;
	ushort x;

	for(x=0; x<workers.wImg; x++) {
		px = *(pxbuf+x);
		key = (((uint)x << 16) | y) & 0x7FFF7FFF;
		spin1_send_fr_packet(key, px, WITH_PAYLOAD);
		//giveDelay(DEF_FR_DELAY);
	}
}

// will use fixed-route packet
void sendResultToTargetFromRoot()
{
	// Note: the SpiNN-link is connected to chip<0,0>
	uchar *imgOut;
	imgOut  = (uchar *)getSdramResultAddr();

	uint rem, sz;
	//experiment: how fast the FR link?
	rem = workers.hImg * workers.wImg;	// send them all!

	// process line by line
	uint dmatag = DMA_FETCH_IMG_TAG | (myCoreID << 16);

	ushort lines = 0;
	do {
		dmaImgFromSDRAMdone = 0;	// will be altered in hDMA
		do {
			dmaTID = spin1_dma_transfer(dmatag, (void *)imgOut,
										(void *)resImgBuf, DMA_READ, workers.wImg);
		} while(dmaTID==0);
		// wait until dma is completed
		while(dmaImgFromSDRAMdone==0) {
		}

		// then sequentially copy & send via fr
		sendImgChunkViaFR(lines, resImgBuf);

		// move to the next address
		lines++;
		imgOut += workers.wImg;
		rem -= workers.wImg;
	} while(rem > 0);}
#endif

/*----------------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------------*/
//             The following is for processing data from other nodes:
#if(DESTINATION==DEST_HOST)
// sendResultToTarget() is scheduled from eHandler.c that processes
// MCPL_SEND_PIXELS_DATA or MCPL_SEND_PIXELS_DATA_DONE
void sendResultToTarget(uint none, uint null)
{
	// first, reduce the block size content
	// it can be positif, zero, of negative because 4-byte boundary problem
	uint sz = sendResultInfo.nReceived_MCPL_SEND_PIXELS * 4;
	sendResultInfo.szBlock -= sz;
	uint alternativeDelay = 1; // --> up to 2.7MBps
	//uint alternativeDelay = 0xFFFFFFFF;	// this also 2.7MBps, but small lost pixels

#if(DEBUG_LEVEL>1)
	io_printf(IO_STD, "[sendResultToTarget] is called!\n"); sark_delay_us(1000);
#endif

	if(sendResultInfo.szBlock >= 0) {
		sendImgChunkViaSDP(sz, alternativeDelay);
		// then trigger the next chain
		if(sendResultInfo.szBlock==0){
			nBlockDone++;
#if(DEBUG_LEVEL>1)
			io_printf(IO_STD, "Sendresult by block-%d done!\n", sendResultInfo.blockToSend);
			sark_delay_us(100);
#endif
			sendResultInfo.blockToSend++;
			spin1_schedule_callback(sendResultChain,
									sendResultInfo.blockToSend, 0,
									PRIORITY_PROCESSING);
		}
		else {
#if(DEBUG_LEVEL>1)
			io_printf(IO_STD, "Preparing to send MCPL_SEND_PIXELS_NEXT for block-%d\n", sendResultInfo.blockToSend);
			sark_delay_us(100);
#endif
			// reset to the beginning of resultMsg.cmd_rc
			sendResultInfo.pxBufPtr = (uchar *)&resultMsg.cmd_rc;
			sendResultInfo.nReceived_MCPL_SEND_PIXELS = 0;
			// send MCPL_SEND_PIXELS_DATA_NEXT
			spin1_send_mc_packet(MCPL_SEND_PIXELS_NEXT,
								 sendResultInfo.blockToSend, WITH_PAYLOAD);
		}
	}
	// means that the remaining pixels is not in 4-byte boundary
	else {
		nBlockDone++;
#if(DEBUG_LEVEL>1)
			io_printf(IO_STD, "Sendresult by block-%d done!\n", sendResultInfo.blockToSend);
			sark_delay_us(100);
#endif
		int rem = 0-sendResultInfo.szBlock;
		sendImgChunkViaSDP((uint)rem, alternativeDelay);
		// then trigger the next chain
		sendResultInfo.blockToSend++;
		spin1_schedule_callback(sendResultChain,
								sendResultInfo.blockToSend, 0,
								PRIORITY_PROCESSING);
	}

}
#else

void sendResultToTarget(uint line, uint null)
{
	// at the moment, we just check the throughput of one chip only
}
#endif
/*----------------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------------*/



/* sendResultProcessCmd() will be called when we receive MCPL_SEND_PIXELS_CMD
 * from root-node. This sendResultProcessCmd() should be executed ONLY by nodes
 * other than the root-node.
 * In this sendResultProcessCmd(), the non-root-node fetch a line from sdram,
 * split it into several MCPL and send them as MCPL_SEND_PIXELS_DATA packets.
 */

/* This is the obsolete method: using line as the parameter...
void sendResultProcessCmd(uint line, uint null)
{
	//io_printf(IO_BUF, "Got MCPL_SEND_PIXELS_CMD line-%d\n", line);
	// first, check if the line is within our block
	if(line>=workers.blkStart && line<=workers.blkEnd) {
#if(DEBUG_LEVEL>1)
		io_printf(IO_STD, "respond to MCPL_SEND_PIXELS_CMD, will send line-%d\n", line); //sark_delay_us(1000);
#endif
		// fetch from sdram
		uchar *imgOut;
		imgOut  = (uchar *)getSdramResultAddr();
		// shift to the current line
		imgOut += (line-workers.blkStart)*workers.wImg;

		uint dmatag = DMA_FETCH_IMG_TAG | (myCoreID << 16);
		dmaImgFromSDRAMdone = 0;	// will be altered in hDMA
		do {
			dmaTID = spin1_dma_transfer(dmatag, (void *)imgOut,
										(void *)resImgBuf, DMA_READ, workers.wImg);
		} while(dmaTID==0);

		// wait until dma is completed
		while(dmaImgFromSDRAMdone==0) {
		}

		// split and send as MCPL packets
		int rem;	// it might be negative
		uint mcplBuf;
		rem = workers.wImg;
		uchar *resPtr = resImgBuf;

		//ushort debuggingCntr = 0;

		do {
			spin1_memcpy((void *)&mcplBuf, resPtr, 4);
			spin1_send_mc_packet(MCPL_SEND_PIXELS_DATA, mcplBuf, WITH_PAYLOAD);
			// without delay, there might be packet drop since congestion
			sark_delay_us(MCPL_DELAY_FACTOR);	// FUTURE: can we optimize this?

			resPtr += 4;
			rem -= 4;
		} while(rem > 0);
	}
}
*/

/* In this version, we use block-ID as the parameter */
void sendResultProcessCmd(uint blockID, uint null)
{
	// check if the line is within our block
	if(blockID != blkInfo->nodeBlockID) return;

#if(DEBUG_LEVEL>1)
			io_printf(IO_STD, "Receiving request for Sendresult for block-%d!\n", blockID);
			sark_delay_us(100);
#endif
	uchar *imgOut;
	imgOut = (uchar *)getSdramResultAddr();

	uint dmatag = DMA_FETCH_IMG_TAG | (myCoreID << 16);

	uchar *pxBuf = sark_alloc(272, 1);	// allocate 272-bytes for pixels
	uint *ptrBuf;

	uint rem, sz, i,j;

	// how many pixels do we have?
	rem = (workers.blkEnd - workers.blkStart + 1) * workers.wImg;

	// first, we need to tell the root-node, how many pixels are in the block
	// so that the root-node knows and can send the correct sdp size
	spin1_send_mc_packet(MCPL_SEND_PIXELS_INFO, rem, WITH_PAYLOAD);

	sz = rem > 272 ? 272 : rem;
	dmaImgFromSDRAMdone = 0;
	do {
		dmaTID = spin1_dma_transfer(dmatag, imgOut, pxBuf, DMA_READ, sz);
	} while(dmaTID==0);
	// then iterate for all pixels in this block:
	do {
		while(dmaImgFromSDRAMdone==0) {
		}

		// split and send as MCPL packets
		// NOTE: flag_SendResultCont will be set in eHandler.c
		flag_SendResultCont = FALSE;

		j = (sz % 4 == 0) ? sz/4 : sz/4 + 1;
		ptrBuf = (uint *)pxBuf;


		/* experiment: use handshaking mechanism, result: SLOW!!!!
		for(i=0; i<j; i++) {
			flag_SendResultCont = FALSE;
			spin1_send_mc_packet(MCPL_SEND_PIXELS_DATA,*ptrBuf, WITH_PAYLOAD);
			ptrBuf++;
			while(flag_SendResultCont == FALSE);
		}
		*/

#if(DEBUG_LEVEL<=1)
		for(i=0; i<j; i++) {
			spin1_send_mc_packet(MCPL_SEND_PIXELS_DATA,
								 (uint)*ptrBuf, WITH_PAYLOAD);
			/*
			if(i%2==0)
				giveDelay(3);	// 2 is OK for small image, but not for big image
			else
				giveDelay(2);
			*/
#if(USING_SPIN==5)
			// TODO: Solve this:
			// Why using Spin5 we need to give delay in order to avoid
			// packet drop while in Spin3, we don't need it?
			// Ans: it must be the BUG in the code...
			giveDelay(3);
#endif
			ptrBuf++;
		}
#else
		for(i=0; i<j; i++) {
			spin1_send_mc_packet(MCPL_SEND_PIXELS_DATA,
								 (uint)*ptrBuf, WITH_PAYLOAD);
			ptrBuf++;
			io_printf(IO_STD, "[sendResultProcessCmd] Send pixel chunk-%d...!\n", i); sark_delay_us(100);
		}
#endif

		imgOut += sz;
		rem -= sz;
		// if there is still data in the block
		if(rem>0) {
			sz = rem > 272 ? 272 : rem;
			dmaImgFromSDRAMdone = 0;
			do {
				dmaTID = spin1_dma_transfer(dmatag, imgOut, pxBuf, DMA_READ, sz);
			} while(dmaTID==0);

			// wait until root-node send the "Go Next" flag...
			while(flag_SendResultCont == FALSE) {
			}
		}

	} while (rem > 0);

	// finally, tell root-node that we've done and release memory
#if(DEBUG_LEVEL>1)
		io_printf(IO_STD, "[sendResultProcessCmd] MCPL_SEND_PIXELS_DONE is sent!\n");
#endif
	spin1_send_mc_packet(MCPL_SEND_PIXELS_DONE, 0, WITH_PAYLOAD);
	sark_free(pxBuf);	// release the 272-bytes buffer
}

/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/



// notifyDestDone() is executed by <0,0,leadAp> only
void notifyDestDone(uint arg0, uint arg1)
{
	perf.tTotal /= blkInfo->maxBlock;
#if (DESTINATION==DEST_HOST)
	resultMsg.srce_addr = elapse;
	resultMsg.srce_port = SDP_SRCE_NOTIFY_PORT;
	resultMsg.length = sizeof(sdp_hdr_t);
	spin1_send_sdp_msg(&resultMsg, 10);
#if (DEBUG_LEVEL > 1)
	io_printf(IO_STD, "SDP_SRCE_NOTIFY_PORT is sent!\nProcessing with %d-nodes is done. Elapse %d-ms, tclk = %u\n",
			  blkInfo->maxBlock, elapse, perf.tTotal);
#endif
#elif (DESTINATION==DEST_FPGA)

#endif
}





// helper function

// debugSDP() displays the sdp content and introduce long delay (1s)
void debugSDP(ushort line, uchar chunkID, ushort nData)
{
	/*
	io_printf(IO_STD, "SDP(hex): ");
	uchar c;
	for(ushort i=0; i<nData; i++) {
		sark_mem_cpy(&c, addr, 1); addr += 1;
		io_printf(IO_STD, "%x ", c);
	}
	io_printf(IO_STD, "\n");
	*/
	//io_printf(IO_STD, "Sending: %d-%d-%d:%d\n",
	//		  blkInfo->nodeBlockID, line, chunkID, nData);
	//sark_delay_us(10000);		// delay 1s
}

/* sendImgChunkViaSDP() is the routine for sending pixels.
 * Before using it, the resultMsg must be filled in with data.
 *
 * The mechanism:
 * - Other node(s) send MCPL to root-node until it send EOC (end of chunk).
 *   These chunks will be put into sdp-buffer directly by the root-node.
 *   Once it receives EOF, it send the sdp and notify the sender to send the
 *   next chunk until that node sends EOB (end of block). Then the root-node
 *   issues the next block triggering.
 * */
// If alternativeDelay==0, then the delay is based on DEF_DEL_VAL, otherwise
// the delay is set to alternativeDelay.
inline void sendImgChunkViaSDP(uint sz, uint alternativeDelay)
{

#if (DEBUG_LEVEL > 0)
	// io_printf(IO_STD, "sending line-%d via sdp...", line);
#endif

	//sendResultInfo.sdpReady = FALSE;

	// use full sdp (scp_segment + data_segment) for pixel values

	/* Formerly, srce_addr contains line number and srce_port...
	 * contains chunkID.
	resultMsg.srce_addr = line;
	resultMsg.srce_port = chunkID;

	*/
	resultMsg.length = sizeof(sdp_hdr_t) + sz;

	spin1_send_sdp_msg(&resultMsg, 10);
	// if alternativeDelay == -1, don't delay!
	if(alternativeDelay!=0xFFFFFFFF) {
		if(alternativeDelay==0)
			giveDelay(sdpDelayFactorSpin);
		//experimen: jika hanya root-node yang kirim gray result
		//giveDelay(1200);	// if 900, this should produce 5.7MBps in 200MHz
		else
			giveDelay(alternativeDelay);
	}

	//sendResultInfo.sdpReady = TRUE;
#if (DEBUG_LEVEL > 0)
	io_printf(IO_STD, "[sendImgChunkViaSDP] done!\n"); sark_delay_us(100);
#endif
}


// getSdramResultAddr() determines the address of final output by reading taskList
// it is intended only for leadAp in each node (not all worker cores)
// it is similar to getSdramImgAddr(), but processed differently: it uses
// workers.blkImg??? instead of workers.img???
uint getSdramResultAddr()
{
	uint imgAddr;
	// by default, it might send gray scale image
	imgAddr = (uint)workers.blkImgOut1;

	// Now iterate: if opType>0, then the output is imgBIn
	if(blkInfo->opType > 0) {
		imgAddr = (uint)workers.blkImgBIn;
	}
	// if not, it depends on opSharpen and opFilter
	else {
		if(blkInfo->opSharpen==1) {
			imgAddr = (uint)workers.blkImgGIn;
		} else {
			if(blkInfo->opFilter==1) {
				imgAddr = (uint)workers.blkImgRIn;
			}
		}
	}
	return imgAddr;
}









/*-------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------*/
/*------------------------ Receiving frames from Source -------------------------*/
