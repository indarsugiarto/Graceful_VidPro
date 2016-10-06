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
	sendResultToTargetFromRoot();

	//experiment: seberapa cepat jika semua dikirim dari node-1
	spin1_schedule_callback(notifyDestDone,0,0,PRIORITY_PROCESSING);
	return;

	// then the remaining nodes should execute sendResultToTarget()
	sendResultInfo.nRemaining_MCPL_SEND_PIXELS = blkInfo->wImg % 4;
	if(sendResultInfo.nRemaining_MCPL_SEND_PIXELS == 0)
		sendResultInfo.nExpected_MCPL_SEND_PIXELS = blkInfo->wImg / 4;
	else
		sendResultInfo.nExpected_MCPL_SEND_PIXELS = blkInfo->wImg / 4 + 1;

#if(DEBUG_LEVEL>0)
	io_printf(IO_STD, "sendResultToTarget()\n");
#endif

	// prepare the buffer

	//what if we allocate sendresultinfo.pxbuf right after frameinfo?
	//sendResultInfo.pxBuf = sark_alloc(sendResultInfo.nExpected_MCPL_SEND_PIXELS, 4);

	// then trigger the chain
	// NOTE: harus event-based, karena spin1_schedule_callback tidak berfungsi!!!

	// prepare the chain:
	sendResultInfo.lineToSend = workers.blkEnd+1;
	//sendResultInfo.adaptiveDelay = MIN_ADAPTIVE_DELAY;	// I found this is reasonable for small image
	//sendResultInfo.delayCntr = 0;
	//sendResultInfo.adaptiveDelay = workers.wImg;
	spin1_schedule_callback(sendResultChain, sendResultInfo.lineToSend, 0, PRIORITY_PROCESSING);

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

	//experiment: berapa cepat jika semua dikirim dari node-1
	//for(ushort lines=workers.blkStart; lines<=workers.blkEnd; lines++) {
	//for(ushort lines=0; lines<=workers.hImg; lines++) {
		uchar chunkID = 0;
		uint rem, sz;
		//rem = (workers.blkEnd - workers.blkStart + 1) * workers.wImg;
		rem = workers.hImg * workers.wImg;	// kirim semuanya!
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
#endif

#if(DESTINATION==DEST_FPGA)

void sendImgChunkViaFR(ushort line, uchar * pxbuf)
{
	sendResultInfo.sdpReady = FALSE;

	uint key;
	uchar px;

	ushort x,y;

	y = line;

	for(x=0; x<workers.wImg; x++) {
		px = *(pxbuf+x);
		key = (((uint)x << 16) | y) & 0x7FFF7FFF;
		spin1_send_fr_packet(key, px, WITH_PAYLOAD);
		//giveDelay(DEF_FR_DELAY);
	}

	sendResultInfo.sdpReady = TRUE;

}

// will use fixed-route packet
void sendResultToTargetFromRoot()
{
	// Note: the SpiNN-link is connected to chip<0,0>
	uchar *imgOut;
	imgOut  = (uchar *)getSdramResultAddr();

	uint dmatag = DMA_FETCH_IMG_TAG | (myCoreID << 16);
	for(ushort lines=workers.blkStart; lines<=workers.blkEnd; lines++) {

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
		imgOut += workers.wImg;
	}
}
#endif

/*----------------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------------*/
//             The following is for processing data from other nodes:
#if(DESTINATION==DEST_HOST)
// sendResultToTarget() is scheduled from eHandler.c that processes
// MCPL_SEND_PIXELS_DATA
void sendResultToTarget(uint line, uint null)
{
	//io_printf(IO_STD, "Root-node got enough data. Send via sdp...\n");

	/* WHY it needs delay before sendImgChunkViaSDP() ? Because...
	Terakhir sampai sini, kenapa kalau delay lebih kecil dari 2000 terus hang....
	karena waktu kirim sdp belum selesai sudah ada data baru dari MCPL
	solusi: pakai flag sendResultInfo.sdpReady
	*/

	// if sdp is not ready, then go to wait state (in recursion-like mechanism)
	if(sendResultInfo.sdpReady==FALSE) {
		/* Do we need delay? Normally NO, but...
		sendResultInfo.delayCntr++;
		if(sendResultInfo.delayCntr>MIN_ADAPTIVE_DELAY) {
			io_printf(IO_STD, "Waiting sdp...\n");
			sendResultInfo.delayCntr = 0;
		}
		else {
			io_printf(IO_BUF, "Waiting sdp...\n");
		}
		// if still too fast, increase the delay
		sendResultInfo.adaptiveDelay += 10;
		sark_delay_us(sendResultInfo.adaptiveDelay);
		*/
		// then call again
		spin1_schedule_callback(sendResultToTarget, line, NULL, PRIORITY_PROCESSING);
	}
	else {

		/* try to reduce the delay adaptively...
		if(sendResultInfo.adaptiveDelay < MIN_ADAPTIVE_DELAY)
			sendResultInfo.adaptiveDelay = MIN_ADAPTIVE_DELAY;
		else sendResultInfo.adaptiveDelay -= 10;
		if(sendResultInfo.adaptiveDelay < workers.wImg)
			sendResultInfo.adaptiveDelay = workers.wImg;
		else sendResultInfo.adaptiveDelay -= 10;
		*/

		// send to target via sdp
		// Indar: batalkan dulu yang dibawah ini sampai experimen dengan
		// single node output selesai.............!!!!!!
		// sendImgChunkViaSDP(line, sendResultInfo.pxBuf, 0);

		// then continue the chain with the next line
		sendResultInfo.lineToSend++;
		spin1_schedule_callback(sendResultChain, sendResultInfo.lineToSend, NULL, PRIORITY_PROCESSING);
	}
}
#endif

#if(DESTINATION==DEST_FPGA)
void sendResultToTarget(uint line, uint null)
{
	// if sdp is not ready, then go to wait state (in recursion-like mechanism)
	if(sendResultInfo.sdpReady==FALSE) {
		// then call again
		spin1_schedule_callback(sendResultToTarget, line, NULL, PRIORITY_PROCESSING);
	}
	else {

		// send to target via sdp
		sendImgChunkViaFR(line, sendResultInfo.pxBuf);

		// then continue the chain with the next line
		sendResultInfo.lineToSend++;
		spin1_schedule_callback(sendResultChain, sendResultInfo.lineToSend, NULL, PRIORITY_PROCESSING);
	}

}
#endif
/*----------------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------------*/



void sendResultChain(uint nextLine, uint unused)
{
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
}



/* sendResultProcessCmd() will be called when we receive MCPL_SEND_PIXELS_CMD
 * from root-node. This sendResultProcessCmd() should be executed ONLY be nodes
 * other than the root-node.
 * In this sendResultProcessCmd(), the non-root-node fetch a line from sdram,
 * split it into several MCPL and send them as MCPL_SEND_PIXELS_DATA packets.
 */
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

/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/



// notifyDestDone() is executed by <0,0,leadAp> only
void notifyDestDone(uint arg0, uint arg1)
{
	perf.tTotal /= blkInfo->maxBlock;
#if (DEBUG_LEVEL > 1)
	io_printf(IO_STD, "Processing with %d-nodes is done. Elapse %d-ms, tclk = %u\n",
			  blkInfo->maxBlock, elapse, perf.tTotal);
#endif
#if (DESTINATION==DEST_HOST)
	resultMsg.srce_addr = elapse;
	resultMsg.srce_port = SDP_SRCE_NOTIFY_PORT;
	resultMsg.length = sizeof(sdp_hdr_t);
	spin1_send_sdp_msg(&resultMsg, 10);
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
	if(alternativeDelay==0)
		giveDelay(sdpDelayFactorSpin);
	//experimen: jika hanya root-node yang kirim gray result
	//giveDelay(1200);	// if 900, this should produce 5.7MBps in 200MHz
	else
		giveDelay(alternativeDelay);

	//sendResultInfo.sdpReady = TRUE;
#if (DEBUG_LEVEL > 0)
	io_printf(IO_STD, "done!\n");
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
