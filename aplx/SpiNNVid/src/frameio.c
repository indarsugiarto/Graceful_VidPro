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
 * In this version, the root-node will buffer all pixel before sending out to the
 * target (host-pc or fpga). While buffering, it also starts the streamer by
 * sending MCPL_SEND_PIXELS_BLOCK_GO_STREAMER to the streamer,
 * and the streamer will start streaming out immediately, assuming that block-0
 * data is ready. While streaming, other nodes are sending data to root-node.
 *
 * The sendResult() triggers the chain mechanism by scheduling sendResultChain().
 * The sendResultChain() will be re-scheduled everytime until all blocks
 * have sent their part.
 *
 *
 * There are two similar subroutine sendResultToTarget and sendResultToTargetFromRoot:
 * - sendResultToTargetFromRoot is dedicated for the root-node and is executed before:
 * - sendResultToTarget is for other nodes
 * Those two subroutines are modified according to the value of DESTINATION
 *
 * NOTE from experiment: use handshaking mechanism with MCPL, result: SLOW!!!!
 *
 *------------------------------------------------------------------------------*/
void sendResult(uint unused, uint arg1)
{

	/* Test by sending only from root-node...
	sendResultToTargetFromRoot();
	notifyDestDone(0,0);
	notifyTaskDone();
	return;
	*/

	// start buffering mechanism

	// first, tell workers in the root-node to prepare receiving pixels
#if(DEBUG_LEVEL>1)
	io_printf(IO_STD, "Tell root-workers to prepare sendResult...\n");
#endif
	// spin1_send_mc_packet(MCPL_SEND_PIXELS_BLOCK_PREP, 0, WITH_PAYLOAD);
	// NOTE: no need for MCPL_SEND_PIXELS_BLOCK_PREP, since the "init" signal
	// will be sent by the sender

	// second, start the chain
	nBlockDone = 1;	// root node has finished its part
	spin1_schedule_callback(sendResultChain, 1, 0, PRIORITY_PROCESSING);

	// third, tell streamer to start the streaming
	// at this point, streamer should have received
	// MCPL_SEND_PIXELS_BLOCK_INFO_STREAMER_DEL (when config received) and
	// MCPL_SEND_PIXELS_BLOCK_INFO_STREAMER_SZIMG (when frame info received)
	uint addr = getSdramBlockResultAddr();
	spin1_send_mc_packet(MCPL_SEND_PIXELS_BLOCK_GO_STREAMER, addr, WITH_PAYLOAD);


	/* The following works in the fixingFilt branch...

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
	*/
}


/* sendResultChain() is the second after the main sendResult()
 * This is to iterate the block other than the root-node.
 * It is called for the first time from sendResult(), then
 * consequtively from hMCPL_SpiNNVid()
 * */
void sendResultChain(uint nextBlock, uint unused)
{

	if(nBlockDone==blkInfo->maxBlock) {
#if(DEBUG_LEVEL>1)
		io_printf(IO_STD, "Notify host and set taskList...\n");
#endif
		// TODO: streaming!!!

		// uint addr = getSdramBlockResultAddr();
		// spin1_send_mc_packet(MCPL_SEND_PIXELS_BLOCK_GO_STREAMER, addr, WITH_PAYLOAD);


		// sendResultToTargetFromRoot();

		// wBuffering debugging: disable host temporary
		// notifyDestDone(0,0); --> its done by streamer!!!
		notifyTaskDone();
	}
	else {
#if(DEBUG_LEVEL>1)
		io_printf(IO_STD, "sendResultChain for block-%d\n", nextBlock);
#endif

		sendResultInfo.blockToSend = nextBlock; // the next block to send is block-1
		spin1_send_mc_packet(MCPL_SEND_PIXELS_BLOCK, sendResultInfo.blockToSend, WITH_PAYLOAD);

		/* This is the working fixingFilt branch...
		sendResultInfo.nReceived_MCPL_SEND_PIXELS = 0;
		sendResultInfo.pxBufPtr = (uchar *)&resultMsg.cmd_rc;
		spin1_send_mc_packet(MCPL_SEND_PIXELS_CMD, nextBlock, WITH_PAYLOAD);
		*/
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

// worker_send_result() is called if worker receives MCPL_SEND_PIXELS_BLOCK_CORES
// from its leadAp, and iterate until all its part is completed. It sends its part
// to root-node (with corresponding core)
void worker_send_result(uint arg0, uint arg1)
{
	ushort l, cntr;
	uchar *imgOut;
	uint dmatag;
	uint key;
	uchar *ptrPx;
	int rem;	// might goes into negative region

	imgOut = (uchar *)getSdramResultAddr();
	dmatag = DMA_FETCH_IMG_TAG | (myCoreID << 16);
	ushort key_arg;
	uint key_hdr = MCPL_SEND_PIXELS_BLOCK_CORES_DATA | (myCoreID << 16);
	uint pl;

	for(l = workers.startLine; l <= workers.endLine; l++) {

		// first, tell receiver that we're going to burst pixels for line-l
		key = MCPL_SEND_PIXELS_BLOCK_CORES_INIT | (myCoreID << 16) | l;
		spin1_send_mc_packet(key, 0, NO_PAYLOAD);

#if(DEBUG_LEVEL>1)
		io_printf(IO_BUF, "Sending line-%d from addr-0x%x...\n", l, (uint)imgOut);
#endif
		key = MCPL_SEND_PIXELS_BLOCK_CORES_DATA | (myCoreID << 16);
		dmaImgFromSDRAMdone = 0;
		do {
			dmaTID = spin1_dma_transfer(dmatag, imgOut, resImgBuf, DMA_READ, workers.wImg);
		} while(dmaTID==0);
		while(dmaImgFromSDRAMdone==0);

		ptrPx = resImgBuf;
		flag_SendResultCont = FALSE;
		rem = workers.wImg;
		do {
			// copy to key_arg
			sark_mem_cpy((void *)&key_arg, (void *)ptrPx, 2);
			key = key_hdr | key_arg;
			//key = key_hdr | 0xFFFF;	// all white
			rem -= 2;
			ptrPx += 2;
			sark_mem_cpy((void *)&pl, (void *)ptrPx, 4);
			spin1_send_mc_packet(key, pl, WITH_PAYLOAD);

			//spin1_send_mc_packet(key, (uint)*ptrPx, WITH_PAYLOAD);	// this creates "zebra" strips
			//spin1_send_mc_packet(key, 0xFFFFFFFF, WITH_PAYLOAD);		// all white
#if(DEBUG_LEVEL==0)
			giveDelay(MCPL_SEND_PIXELS_BLOCK_DELAY);
#else
			io_printf(IO_BUF, "Send key-0x%x, pl-0x%x\n", key,(uint)*ptrPx);
			sark_delay_us(10000);
#endif
			ptrPx += 4;
			rem -= 4;

		} while(rem > 0);
		while(flag_SendResultCont == FALSE);
		imgOut += workers.wImg;
	}
	spin1_send_mc_packet(MCPL_SEND_PIXELS_BLOCK_CORES_DONE, 0, WITH_PAYLOAD);
}

// worker_recv_result() is called when a worker in the root-node receives the complete
// line from other node and it will send MCPL_SEND_PIXELS_BLOCK_CORES_NEXT
void worker_recv_result(uint arg0, uint arg1)
{
	// step-0: determine the address of the line
	uchar *imgAddr = (uchar *)getSdramBlockResultAddr();
	uchar *lineAddr = imgAddr + sendResultInfo.cl*workers.wImg;

	// pause di sini, mau lihat apakah alamatnya sama:
#if(DEBUG_LEVEL>2)
	io_printf(IO_BUF, "imgAddr = 0x%x, lineAddr = 0x%x\n", imgAddr, lineAddr);
	// return; // --> OK!, after revising the computeWLoad() in process.c
#endif

	// step-1: put into sdram
	uint dmatag = DMA_STORE_IMG_TAG | (myCoreID << 16);
	do {
		dmaTID = spin1_dma_transfer(dmatag, lineAddr, resImgBuf,
									DMA_WRITE, workers.wImg);
#if(DEBUG_LEVEL>0)
		if(dmaTID==0)
			io_printf(IO_BUF, "Dma full! Retry...\n");
#endif
	} while(dmaTID==0);

	// step-2: reset counter
	/*
	sendResultInfo.nReceived_MCPL_SEND_PIXELS = 0;
	sendResultInfo.pxBufPtr = resImgBuf;
	*/

	// step-3: send "next" signal
#if(DEBUG_LEVEL > 2)
	//io_printf(IO_BUF, "Sending NEXT to core-%d\n", myCoreID);
	io_printf(IO_BUF, "DMA done! Ready to send NEXT to core-%d\n", myCoreID);
#endif

	uint key = MCPL_SEND_PIXELS_BLOCK_CORES_NEXT | (myCoreID << 16);
	spin1_send_mc_packet(key, 0, NO_PAYLOAD);
}



// getSdramResultAddr() determines the address of final output by reading taskList
// it is intended only for leadAp in each node (not all worker cores)
// it is similar to getSdramImgAddr(), but processed differently: it uses
// workers.blkImg??? instead of workers.img???
uint getSdramResultAddr()
{
	/* The following will result in the same line over and over...
	uint imgAddr;
	// by default (ie. dvs is not activated yet), it might send gray scale image
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
	*/

	uint imgAddr;
	// by default (ie. dvs is not activated yet), it might send gray scale image
	imgAddr = (uint)workers.imgOut1;

	// Now iterate: if opType>0, then the output is imgBIn
	if(blkInfo->opType > 0) {
		imgAddr = (uint)workers.imgBIn;
	}
	// if not, it depends on opSharpen and opFilter
	else {
		if(blkInfo->opSharpen==1) {
			imgAddr = (uint)workers.imgGIn;
		} else {
			if(blkInfo->opFilter==1) {
				imgAddr = (uint)workers.imgRIn;
			}
		}
	}
	return imgAddr;
}


// getSdramBlockResultAddr() is similar to getSdramResultAddr(), but only
// being used by root-node
uint getSdramBlockResultAddr()
{
	uint imgAddr;
	// by default (ie. dvs is not activated yet), it might send gray scale image
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
