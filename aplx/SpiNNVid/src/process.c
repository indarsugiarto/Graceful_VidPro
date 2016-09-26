// All distributed processing mechanisms are in this file
#include "SpiNNVid.h"
#include <stdlib.h>	// need for abs()


void initIDcollection(uint withBlkInfo, uint Unused)
{
	// We begin by counting, how many workers are there, so that we can know exactly
	// how many ping-reply is expected from workers

	// Found that Nworkers is different before and after spin1_start()
	blkInfo->Nworkers = get_Nworkers();
#if (DEBUG_LEVEL>2)
	io_printf(IO_STD, "Nworkers = %d\n", blkInfo->Nworkers);
#endif


#if(DEBUG_LEVEL > 0)
	io_printf(IO_BUF, "[SpiNNVid] initIDcollection...\n");
#endif
	spin1_send_mc_packet(MCPL_BCAST_INFO_KEY, 0, WITH_PAYLOAD);

	// if withBlkInfo is True, then we need to broadcast blkInfo
	// Note: Don't use it DURING run time:
	//       during run time, blkInfo has the same content
	if(withBlkInfo==TRUE)
		spin1_send_mc_packet(MCPL_BCAST_INFO_KEY, (uint)blkInfo, WITH_PAYLOAD);
}




// bcastWID() will be called by leadAp from hMCPL() if it has received
// all ID from other cores
void bcastWID(uint Unused, uint null)
{
#if(DEBUG_LEVEL > 0)
	io_printf(IO_BUF, "[SpiNNVid] Distributing wIDs...\n");
#endif
	// payload.high = tAvailable, payload.low = wID
	for(uint i= 1; i<workers.tAvailable; i++)	// excluding leadAp
		spin1_send_mc_packet(workers.wID[i], (workers.tAvailable << 16) + i, WITH_PAYLOAD);

	// then broadcast blkInfo address
	spin1_send_mc_packet(MCPL_BCAST_INFO_KEY, (uint)blkInfo, WITH_PAYLOAD);

	// for debugging (however, the output depends on DEBUG_LEVEL)
	// spin1_schedule_callback(give_report, DEBUG_REPORT_WID, 0, PRIORITY_PROCESSING);
}



// computeWLoad will be executed by all cores (including leadAps) in all chips
void computeWLoad(uint withReport, uint arg1)
{
	// if the node is not involved, simply disable the core
	if(blkInfo->maxBlock==0) {
		workers.active = FALSE;
		return;
	}

	//io_printf(IO_BUF, "Computing workload...\n");
	// keep local (for each core) and to speed up
	ushort i,j;
	ushort w = blkInfo->wImg; workers.wImg = w;
	ushort h = blkInfo->hImg; workers.hImg = h;
	ushort maxBlock = blkInfo->maxBlock;
	ushort nodeBlockID = blkInfo->nodeBlockID;

	/*--------------------------------------------------------------------------*/
	/*----------- let's distribute the region even-ly accross nodes ------------*/
	ushort wl[48], sp[48], ep[48];	// assuming 48 nodes at max
	// first, make "default" block-size. Here wl contains the block-size:
	ushort nLinesPerBlock = h / maxBlock;
	ushort nRemInBlock = h % maxBlock;
	for(i=0; i<maxBlock; i++) {
		wl[i] = nLinesPerBlock;
	}
	// then distribute the remaining part
	i = 0;
	while(nRemInBlock > 0) {
		wl[i]++; i++; nRemInBlock--;
	}
	// then compute the blkStart according to wl (block-size)
	ushort blkStart, blkEnd;
	// for root, it should start at 0 position
	if(sv->p2p_addr==0)	{
		blkStart = 0;
		blkEnd = wl[0] - 1;
	}
	else {
		blkStart = nodeBlockID*wl[nodeBlockID-1];
		blkEnd = blkStart + wl[nodeBlockID] - 1;
	}
	// then locally record these block info
	workers.nLinesPerBlock = wl[nodeBlockID];
	workers.blkStart = blkStart;
	workers.blkEnd = blkEnd;

	/*--------------------------------------------------------------------------*/
	/*----------------- then compute local region for each core ----------------*/
	// if there's remaining lines, then give them to the last block
//	if(blkInfo->nodeBlockID==blkInfo->maxBlock-1)
//		workers.nLinesPerBlock += nRemInBlock;
//	workers.blkEnd = workers.blkStart + workers.nLinesPerBlock - 1;

	// core-wide
	ushort nLinesPerCore;
	ushort nRemInCore;
	// if the number of available cores is less then or equal total lines per block:
	if(workers.tAvailable <= workers.nLinesPerBlock) {
		workers.tRunning = workers.tAvailable;	// useful for leadAp only!

		nLinesPerCore = workers.nLinesPerBlock / workers.tAvailable;
		nRemInCore = workers.nLinesPerBlock % workers.tAvailable;

		// initialize starting point with respect to blkStart
		for(i=0; i<17; i++)
			sp[i] = workers.blkStart;

		// then adjust to the actual number of cores in the chip
		for(i=0; i<workers.tAvailable; i++) {
			wl[i] = nLinesPerCore;
			if(nRemInCore > 0) {
				wl[i]++;
				for(j=i+1; j<workers.tAvailable; j++) {
					sp[j]++;
				}
				nRemInCore--;
			}
			sp[i] += i*nLinesPerCore;
			ep[i] = sp[i]+wl[i]-1;
		}
		workers.startLine = sp[workers.subBlockID];
		workers.endLine = ep[workers.subBlockID];
		workers.active = TRUE;
	}
	// if the number of available cores is higher, then select some cores only
	else {
		// leadAp needs to know, how many actual workers will be working
		workers.tRunning = workers.nLinesPerBlock;

		// select some workers while disable unused one
		if(workers.subBlockID < workers.nLinesPerBlock) {
			workers.startLine = workers.blkStart + workers.subBlockID;
			workers.endLine = workers.startLine;
			workers.active = TRUE;
		}
		else {
			workers.active = FALSE;
		}
	}

	// then align the internal/worker pointer accordingly
	workers.imgRIn = blkInfo->imgRIn + w*workers.startLine;
	workers.imgGIn = blkInfo->imgGIn + w*workers.startLine;
	workers.imgBIn = blkInfo->imgBIn + w*workers.startLine;
	workers.imgOut1 = blkInfo->imgOut1 + w*workers.startLine;
	//workers.imgOut2 = blkInfo->imgOut2 + w*workers.startLine;
	//workers.imgOut3 = blkInfo->imgOut3 + w*workers.startLine;
	// so, each work has different value of those workers.img*

	// leadAp needs to know, the address of image block
	// it will be used for sending result to host-PC
	if(myCoreID == LEAD_CORE) {
		uint szBlk = (workers.blkEnd - workers.blkStart + 1) * workers.wImg;
		uint offset = blkInfo->nodeBlockID * szBlk;
		workers.blkImgRIn = blkInfo->imgRIn + offset;
		workers.blkImgGIn = blkInfo->imgGIn + offset;
		workers.blkImgBIn = blkInfo->imgBIn + offset;
		workers.blkImgOut1 = blkInfo->imgOut1 + offset;
		//workers.blkImgOut2 = blkInfo->imgOut2 + offset;
		//workers.blkImgOut3 = blkInfo->imgOut3 + offset;
	}

	// other locally copied data (in DTCM, rather than in SysRam)
	workers.opFilter = blkInfo->opFilter;
	workers.opType = blkInfo->opType;
	workers.opSharpen = blkInfo->opSharpen;

	// prepare the pixel buffers
	ushort szMask = blkInfo->opType == IMG_SOBEL ? 3:5;
	workers.szDtcmImgBuf = szMask * w;

	// when first called, dtcmImgBuf should be NULL. It is initialized in SpiNNVid_main()
	if(dtcmImgBuf != NULL) {
		io_printf(IO_BUF, "[IMGBUF] Releasing DTCM heap...\n");
		sark_free(dtcmImgBuf);
		sark_free(resImgBuf);
	}
	// dtcmImgBuf can be 3 or 5 times the image width
	dtcmImgBuf = sark_alloc(workers.szDtcmImgBuf, sizeof(uchar));
	resImgBuf = sark_alloc(w, sizeof(uchar));	// just one line!
	//io_printf(IO_BUF, "my startline = %d, my endline = %d\n", workers.startLine, workers.endLine);

	// debugging:
	//give_report(DEBUG_REPORT_WLOAD, 1);

	// IT IS WRONG to put the initHistData() here, because initHistData MUST
	// BE CALLED everytime a new image has arrived. So, we move it to processGrayScaling()

}


// processGrayScaling() will process pxBuffer
void processGrayScaling(uint arg0, uint arg1)
{
	/* Problem with uchar and stdfix:
	 * Somehow, uchar is treated weirdly by stdfix. For example, if rpxBuf[0]==255:
	 * printing (REAL)rpxBuf[0] = %k -> will result in -1.0000000
	 * BUT, if I define:
	 * uchar c = 255;
	 * then printing (REAL)c = %k -> will produce 255.00000 !!!! WTF!!!
	 * Solution: create temporary variable other than uchar
	 * */
	REAL tmp;
	ushort v1,v2,v3;
	uchar grVal;
	for(ushort i=0; i<pxBuffer.pxLen; i++) {
		/*
		v1 = pxBuffer.rpxbuf[i];
		v2 = pxBuffer.gpxbuf[i];
		v3 = pxBuffer.bpxbuf[i];
		*/
		v1 = rpxbuf[i];
		v2 = gpxbuf[i];
		v3 = bpxbuf[i];

		/*
		tmp = (REAL)pxBuffer.rpxbuf[i] * R_GRAY +
			  (REAL)pxBuffer.gpxbuf[i] * G_GRAY +
			  (REAL)pxBuffer.bpxbuf[i] * B_GRAY;
		*/
		tmp = (REAL)v1*R_GRAY + (REAL)v2*G_GRAY + (REAL)v3*B_GRAY;
		// why round first? because if not, it truncate to lower integer
		grVal = (uchar)roundr(tmp);
		//pxBuffer.ypxbuf[i] = grVal;
		ypxbuf[i] = grVal;
	}
	// then copy to sdram determined by pxBuffer.pxSeq
	// Note: blkInfo->imgOut1 must be initialized already and all cores must
	//       know it already (ie. blkInfo must be broadcasted in advance)

	// Assuming that we only work with "non icon" image, then the chunk size
	// will be the same, unless the last one. Hence, we put the fix-size here:
	uint offset = pxBuffer.pxSeq*DEF_PXLEN_IN_CHUNK;


	// io_printf(IO_STD, "pxSeq = %d, offset = 0x%x\n", pxBuffer.pxSeq, offset);


	// at this point, although workers.imgRIn... workers.imgOut1... have been initialized,
	// we cannot use for storing original image to sdram, since the image distribution
	// is independent of wID !!! So, we have to use blkInfo, which is global for all cores
	// in the chip (it IS independent of wID)

	// coba dengan manual copy (bukan DMA): hasilnya? SEMPURNA !!!!
	/*
	sark_mem_cpy((void *)blkInfo->imgRIn+offset, (void *)rpxbuf, pxBuffer.pxLen);
	sark_mem_cpy((void *)blkInfo->imgGIn+offset, (void *)gpxbuf, pxBuffer.pxLen);
	sark_mem_cpy((void *)blkInfo->imgBIn+offset, (void *)bpxbuf, pxBuffer.pxLen);
	sark_mem_cpy((void *)blkInfo->imgOut1+offset, (void *)ypxbuf, pxBuffer.pxLen);
	*/

	// wait until we get the token


	//io_printf(IO_BUF, "Storing pixels via DMA...\n");
	//io_printf(IO_BUF, "dmaToken_pxStore = %d\n", blkInfo->dmaToken_pxStore);
	while(blkInfo->dmaToken_pxStore != myCoreID) {
	}
	//io_printf(IO_BUF, "dmaToken_pxStore = %d\n", blkInfo->dmaToken_pxStore);

	// the combination of dma and direct memory copying
	uint dmaDone, tg;
	do {
		tg = (myCoreID << 16) | DMA_TAG_STORE_R_PIXELS;
		dmaDone = spin1_dma_transfer(tg, blkInfo->imgRIn+offset,
									 rpxbuf, DMA_WRITE, pxBuffer.pxLen);
	} while(dmaDone==0);
	// wait until dma is complete

	//io_printf(IO_BUF, "dmaDone_rpxStore = %d\n", blkInfo->dmaDone_rpxStore);
	while(blkInfo->dmaDone_rpxStore != myCoreID) {
	}
	//io_printf(IO_BUF, "dmaDone_rpxStore = %d\n", blkInfo->dmaDone_rpxStore);

	blkInfo->dmaDone_rpxStore = 0;	// reset to avoid ambiguity
	do {
		tg = (myCoreID << 16) | DMA_TAG_STORE_G_PIXELS;
		dmaDone = spin1_dma_transfer(tg, blkInfo->imgGIn+offset,
									 gpxbuf, DMA_WRITE, pxBuffer.pxLen);
	} while(dmaDone==0);
	// wait until dma is complete

	//io_printf(IO_BUF, "dmaDone_gpxStore = %d\n", blkInfo->dmaDone_gpxStore);
	while(blkInfo->dmaDone_gpxStore != myCoreID) {
	}
	//io_printf(IO_BUF, "dmaDone_gpxStore = %d\n", blkInfo->dmaDone_gpxStore);

	blkInfo->dmaDone_gpxStore = 0;	// reset to avoid ambiguity
	do {
		tg = (myCoreID << 16) | DMA_TAG_STORE_B_PIXELS;

		dmaDone = spin1_dma_transfer(tg, blkInfo->imgBIn+offset,
									 bpxbuf, DMA_WRITE, pxBuffer.pxLen);
	} while(dmaDone==0);

	// wait until dma is complete
	//io_printf(IO_BUF, "dmaDone_bpxStore = %d\n", blkInfo->dmaDone_bpxStore);
	while(blkInfo->dmaDone_bpxStore != myCoreID) {
	}
	//io_printf(IO_BUF, "dmaDone_bpxStore = %d\n", blkInfo->dmaDone_bpxStore);

	blkInfo->dmaDone_bpxStore = 0;	// reset to avoid ambiguity
	do {
		tg = (myCoreID << 16) | DMA_TAG_STORE_Y_PIXELS;

		dmaDone = spin1_dma_transfer(tg, blkInfo->imgOut1+offset,
									 ypxbuf, DMA_WRITE, pxBuffer.pxLen);
	} while(dmaDone==0);

	// then release the token
	if((blkInfo->dmaToken_pxStore - 1) == NUM_CORES_FOR_BCAST_PIXEL)
		blkInfo->dmaToken_pxStore = LEAD_CORE;
	else
		blkInfo->dmaToken_pxStore++;

	//io_printf(IO_BUF, "dmaToken_pxStore = %d\n", blkInfo->dmaToken_pxStore);

	/*------------------------ debugging --------------------------*/
	// debugging: to see if original pixels chunks and forwarded are the same
	// in python, we use raw_input() to get a chance for viewing it
	// seePxBuffer(IO_BUF);

	/*
	// see the address where the chunk is stored
	if(sv->p2p_addr==0)
	io_printf(IO_STD, "rgby-chunk-%d is stored at 0x%x, 0x%x, 0x%x, 0x%x\n", pxBuffer.pxSeq,
			  blkInfo->imgRIn+offset,
			  blkInfo->imgGIn+offset,
			  blkInfo->imgBIn+offset,
			  blkInfo->imgOut1+offset);
	*/

	// give a delay for dma to complete
	// sark_delay_us(1000);
	// then peek
	// peekPxBufferInSDRAM();

	// Karena srce_addr tidak bisa dipakai untuk pxSeq, kita buat pxSeq secara
	// sekuensial:
	// pxBuffer.pxSeq++;	// dia akan di-reset di bagian hSDP()

	// then forward the pixels to similar cores but in other chips
	if(sv->p2p_addr==0)
		spin1_schedule_callback(fwdImgData,0,0,PRIORITY_PROCESSING);




	/* Debugging 16 Agustus: matikan dulu histogram-nya
	// final step: reset histogram data
	if(newImageFlag==TRUE)
		// here, histPropTree will be constructed
		spin1_schedule_callback(initHistData, 0, 0, PRIORITY_PROCESSING);
	else
		spin1_schedule_callback(computeHist, 0, 0, PRIORITY_PROCESSING);
	// then reset the newImageFlag
	*/

	newImageFlag=FALSE;

	// TODO: histogram counting

}

// once the core receive chunks for all channels, it forwards them
void fwdImgData(uint arg0, uint arg1)
{
	uint payload, i, cntr, szpx, remaining;
	// payload contains how many pixels and the current chunk-ID
	payload = (pxBuffer.pxLen << 16) + pxBuffer.pxSeq;
	cntr = (pxBuffer.pxLen % 4) == 0 ? pxBuffer.pxLen / 4 : (pxBuffer.pxLen / 4) + 1;

	// first forward chunk information
	spin1_send_mc_packet(MCPL_FWD_PIXEL_INFO+myCoreID, payload, WITH_PAYLOAD);

#if (FWD_FULL_COLOR==TRUE)
	uint ch;
	// then forward chunks for each channels
	for(ch=0; ch<3; ch++) {
		remaining = pxBuffer.pxLen;
		for(i=0; i<cntr; i++) {
			if(remaining > sizeof(uint))
				szpx = sizeof(uint);
			else
				szpx = remaining;
			if(ch==0) {
				//sark_mem_cpy((void *)&payload, (void *)pxBuffer.rpxbuf + i*4, szpx);
				sark_mem_cpy((void *)&payload, (void *)rpxbuf + i*4, szpx);
				// note the 4-byte boundary above; hence, we define rpxbuf as 272 array
				spin1_send_mc_packet(MCPL_FWD_PIXEL_RDATA+myCoreID, payload, WITH_PAYLOAD);
			} else if(ch==1) {
				//sark_mem_cpy((void *)&payload, (void *)pxBuffer.gpxbuf + i*4, szpx);
				sark_mem_cpy((void *)&payload, (void *)gpxbuf + i*4, szpx);
				// note the 4-byte boundary above; hence, we define rpxbuf as 272 array
				spin1_send_mc_packet(MCPL_FWD_PIXEL_GDATA+myCoreID, payload, WITH_PAYLOAD);
			} else {
				//sark_mem_cpy((void *)&payload, (void *)pxBuffer.bpxbuf + i*4, szpx);
				sark_mem_cpy((void *)&payload, (void *)bpxbuf + i*4, szpx);
				// note the 4-byte boundary above; hence, we define rpxbuf as 272 array
				spin1_send_mc_packet(MCPL_FWD_PIXEL_BDATA+myCoreID, payload, WITH_PAYLOAD);
			}
			remaining -= szpx;

		}
	}

#else
	/*-------------- gray pixels forwarding ----------------*/


	// io_printf(IO_BUF, "Forwarding gray pixels...\n");


	remaining = pxBuffer.pxLen;
	for(i=0; i<cntr; i++) {
		if(remaining > sizeof(uint))
			szpx = sizeof(uint);
		else
			szpx = remaining;
		sark_mem_cpy((void *)&payload, (void *)ypxbuf + i*4, szpx);
		spin1_send_mc_packet(MCPL_FWD_PIXEL_YDATA+myCoreID, payload, WITH_PAYLOAD);
		remaining -= szpx;
	}

	// then, send info end-of-forwarding
	spin1_send_mc_packet(MCPL_FWD_PIXEL_EOF+myCoreID, 0, WITH_PAYLOAD);
#endif
}


// collectGrayPixels() will be executed by other chips when
// all gray-pixel packets from root chip have been received
void collectGrayPixels(uint arg0, uint arg1)
{

	// io_printf(IO_BUF, "Collecting gray pixels...\n");

	uint offset = pxBuffer.pxSeq*DEF_PXLEN_IN_CHUNK;
	uint dmaDone;
	// via dma or direct copy?
	// I think we don't need to use token here, the processing in root-node must
	// take some time before the next core-broadcasting takes place
	do {
		dmaDone = spin1_dma_transfer(DMA_TAG_STORE_Y_PIXELS+myCoreID,
									 blkInfo->imgOut1+offset,
									 ypxbuf, DMA_WRITE, pxBuffer.pxLen);
	} while(dmaDone==0);

	// coba dengan manual copy: hasilnya? SEMPURNA !!!!
	// sark_mem_cpy((void *)blkInfo->imgOut1+offset, (void *)ypxbuf, pxBuffer.pxLen);




	/* Debugging 16 Agustus: matikan dulu histogram-nya

	// then compute the histogram
	// this will be performed by (almost) all cores in the chip that receive
	// the gray pixels from root node
	// final step: reset histogram data
	if(newImageFlag==TRUE)
		initHistData(0,0);		// here, histPropTree will be constructed
	else
		computeHist(0,0);
	// then reset newImageFlag
	*/


	newImageFlag=FALSE;
}

/*------------------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------------------*/
/*----------------------------- IMAGE PROCESSING CORE MECHANISMS ---------------------------*/
/*-----------------------------                                  ---------------------------*/

// taskProcessingLoop() is scheduled when leadAp-root receives "End-of-Frame" via
// SDP_PORT_FRAME_END. It will be re-scheduled over time.
void taskProcessingLoop(uint arg0, uint arg1)
{

	// NOTE: task increment is done directly in eHandler.c

	if(taskList.cTaskPtr < taskList.nTasks){
#if (DEBUG_LEVEL > 0)
		io_printf(IO_STD, "[SpiNNVid] Processing taskID-%d begin...\n", (uint)taskList.cTask);
#endif
		// broadcast the current task to all cores...
		// the MCPL_BCAST_START_PROC will trigger triggerProcessing(taskID)
		spin1_send_mc_packet(MCPL_BCAST_START_PROC, (uint)taskList.cTask, WITH_PAYLOAD);
	}
}


/*  LeadAp in root-node will broadcast MCPL_BCAST_START_PROC to all cores that triggers
	triggerProcessing(). The "taskID" is the processing mode that is used for synchronization.

	This way, triggerProcessing() is a kind of centralized starting point for
	different processing steps, which is activated as event-based (via MCPL event).

	LeadAp in the root-node manage a task list and use it to synchronize tasks in the net.
	During this triggerProcessing:
	- identify what processing is required

	See also triggerProcessing() in the museum
*/
void triggerProcessing(uint taskID, uint arg1)
{
	// prepare measurement
	// tic = sv->clock_ms;
	perf.tCore = 0;
	perf.tNode = 0;
	perf.tTotal = 0;

	// how many workers/blocks have finished the process?
	nWorkerDone = 0;
	nBlockDone = 0;

	switch((proc_t)taskID){
	case PROC_FILTERING:
		break;
	case PROC_SHARPENING:
		break;
	case PROC_EDGING_DVS:
		spin1_schedule_callback(imgDetection, 0, 0, PRIORITY_PROCESSING);
		break;
	case PROC_SEND_RESULT:
		// if I'm the root-node, send the result right away
		if(sv->p2p_addr == 0) {
			// start the chain by triggering MCPL_BCAST_SEND_RESULT with payload "0"
			sendResult(0,0);
			//spin1_send_mc_packet(MCPL_BCAST_SEND_RESULT, blkInfo->nodeBlockID, WITH_PAYLOAD);
		}
		// otherwise, wait for signal from the root-node
		break;
	}


	if(myCoreID==LEAD_CORE && (proc_t)taskID!=PROC_SEND_RESULT) {
		// inform profiler that the processing is started (for adaptive freq?)
		spin1_send_mc_packet(MCPL_TO_OWN_PROFILER, PROF_MSG_PROC_START, WITH_PAYLOAD);
	}

	/*
	if(blkInfo->opSharpen==1) {
		proc = PROC_HISTEQ;
		// only if root-node and leadAp, trigger the histogram equalisation chain
		if(sv->p2p_addr==0 && leadAp)

			// start timer to measure time
			START_TIMER();

			spin1_send_mc_packet(MCPL_BCAST_REPORT_HIST, 0, WITH_PAYLOAD);
			// MCPL_BCAST_REPORT_HIST will be broadcasted to all cores in all chips
			// only core-1 to core-4 in all chips will response to this call, because
			// for sending 256 item of uint, we require 4 active "thread". Hence, we assign
			// 4 cores to do this.
	}
	else if(blkInfo->opFilter==1) {
		proc = PROC_FILTERING;
		imgFiltering(0,0);	// inside imgFiltering, it will then call imgDetection
	}
	else {
		proc = PROC_EDGING;
#if (adaptiveFreq==TRUE)
		//changeFreq(250);
		imgDetection(0,0);	// go edge detection directly
		//changeFreq(200);
#else
		imgDetection(0,0);	// go edge detection directly
#endif
	}

	*/

	// then reset newImageFlag so that the next frame can be detected properly
	newImageFlag = TRUE;

	// QUESTION: when do we go to taskProcessingLoop()???
	// TODO: make sure that it goes back to taskProcessingLoop() after each process
}

// getSdramImgAddr() determines sdramImgIn and sdramImgOut based on the given
// processing mode "proc" that will be accessed by the working cores!!!
// Hence, we need workers.img??? and not workers.blkImg???
void getSdramImgAddr(proc_t proc)
{
	/* The logic:
	 * - gray scaling output is in Y-buf (imgbuf3)
	 * - filtering output is always in R-buf (imgbuf0)
	 * - sharpening output is always in G-buf (imgbuf1)
	 * - edging output is always in B-buf (imgbuf2)
	 * + for filtering, the input in always Y-buf (imgbuf3)
	 * + for sharpening, if filtering is ON then input is R-buf (imgbuf0), otherwise
	 *   the input is Y-buf (imgbuf3)
	 * + for edging, if sharpening is ON then the input is always G-buf (imgbuf1), otherwise
	 *   if filtering is ON, then the input is R-buf (imgbuf0), BUT if filtering is off
	 *   then the input is Y-buf (imgbuf3)
	*/
	switch(proc){
	case PROC_FILTERING:
		workers.sdramImgIn = workers.imgOut1;
		workers.sdramImgOut = workers.imgRIn;
		break;
	case PROC_SHARPENING:
		workers.sdramImgOut = workers.imgGIn;
		if(workers.opFilter==1)
			workers.sdramImgIn = workers.imgRIn;
		else
			workers.sdramImgIn = workers.imgOut1;
		break;
	case PROC_EDGING_DVS:
		workers.sdramImgOut = workers.imgBIn;
		if(workers.opSharpen==1) {
			workers.sdramImgIn = workers.imgGIn;
		} else {
			if(workers.opFilter==1)
				workers.sdramImgIn = workers.imgRIn;
			else
				workers.sdramImgIn = workers.imgOut1;
		}
	}
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

void imgSharpening(uint arg0, uint arg1)
{
	if(workers.active==FALSE) {
		io_printf(IO_BUF, "I'm disabled!\n");
		return;
	}
}

void imgFiltering(uint arg0, uint arg1)
{
	if(workers.active==FALSE) {
		io_printf(IO_BUF, "I'm disabled!\n");
		return;
	}
	// step-1: do filtering

	// step-2: call imgDetection
}

// NOTE: in imgDetection(), we have mode==SOBEL(1), LAPLACE(2)
void imgDetection(uint arg0, uint arg1)
{
	if(workers.active==FALSE) {
		io_printf(IO_BUF, "I'm disabled!\n");
		return;
	}

	// Use timer-2 to measure the performance
	START_TIMER();

	ushort offset = workers.opType == IMG_SOBEL ? 1:2;
	offset *= workers.wImg;

	short l,c,n,i,j;
	uchar *dtcmLine;	//point to the current image line in the DTCM (not in SDRAM!)
	//uchar *debugging;
	int sumX, sumY, sumXY;

	uint dmatag;

	// how many lines this worker has?
	n = workers.endLine - workers.startLine + 1;

	// prepare the correct line address in sdram
	getSdramImgAddr(PROC_EDGING_DVS);

	// scan for all lines in the worker's working block
	for(l=0; l<n; l++) {

		dmaImgFromSDRAMdone = 0;
		dmatag = DMA_FETCH_IMG_TAG | (myCoreID << 16);
		do {
			dmaTID = spin1_dma_transfer(dmatag, (void *)workers.sdramImgIn - offset,
										(void *)dtcmImgBuf, DMA_READ, workers.szDtcmImgBuf);
			if(dmaTID==0)
				io_printf(IO_BUF, "[Edging] DMA full for tag-0x%x! Retry...\n", dmatag);
		} while (dmaTID==0);

		// wait until dma above is completed
		while(dmaImgFromSDRAMdone==0) {
		}

		//sark_mem_cpy(dtcmImgBuf, sdramImgIn - offset, workers.szDtcmImgBuf);

		// point to the current image line in the DTCM (not in SDRAM!)
		dtcmLine = dtcmImgBuf + offset;	// faster, but the result is "darker"
		//dtcmLine = dtcmImgBuf;	// Aneh, ini hasilnya bagus :(
		//dtcmLine = sdramImgIn;	// very slow, but the result is "better"

		/*
		debugging = sdramImgIn;
		ushort _cntr = 0;
		for(ushort chk=0; chk<workers.wImg; chk++) {
			if(dtcmLine[chk] != debugging[chk]) {
				io_printf(IO_BUF, "Different at offset %d\n", chk);
				_cntr++;
			}
		}
		io_printf(IO_BUF, "Found %d mismatch!\n", _cntr);
		*/

		// scan for all column in the line
		for(c=0; c<workers.wImg; c++) {
			// if offset is 1, then it is for sobel, otherwise it is for laplace
			if(offset==1) {
				sumX = 0;
				sumY = 0;
				if(workers.startLine+l == 0 || workers.startLine+l == workers.hImg-1)
					sumXY = 0;
				else if(c==0 || c==workers.wImg-1)
					sumXY = 0;
				else {
					for(i=-1; i<=1; i++)
						for(j=-1; j<=1; j++) {
							sumX += (int)((*(dtcmLine + c + i + j*workers.wImg)) * GX[i+1][j+1]);
							sumY += (int)((*(dtcmLine + c + i + j*workers.wImg)) * GY[i+1][j+1]);
						}
					// python version: sumXY[0] = math.sqrt(math.pow(sumX[0],2) + math.pow(sumY[0],2))
					sumXY = (abs(sumX) + abs(sumY))*7/10;	// 7/10 = 0.717 -> cukup dekat dengan akar
				}
			}
			else {	// for laplace operation
				sumXY = 0;
				if((workers.startLine+l) < 2 || (workers.hImg-workers.startLine+l) <= 2)
					sumXY = 0;
				else if(c<2 || (workers.wImg-c)<=2)
					sumXY = 0;
				else {
					for(i=-1; i<=2; i++)
						for(j=-2; j<=2; j++)
							sumXY += (int)((*(dtcmLine + c + i + j*workers.wImg)) * LAP[i+2][j+2]);
				}
			}

			// make necessary correction
			if(sumXY>255) sumXY = 255;
			if(sumXY<0) sumXY = 0;

			// resImgBuf is just one line and it doesn't matter, where it is!
			//*(resImgBuf + c) = 255 - (uchar)(sumXY);
			*(resImgBuf + c) = (uchar)(sumXY);

		} // end for c-loop

		// then copy the resulting line into sdram

		//sark_mem_cpy((void *)sdramImgOut, (void *)resImgBuf, workers.wImg);

		dmatag = (myCoreID << 16) | DMA_STORE_IMG_TAG;
		do {
			dmaTID = spin1_dma_transfer(dmatag, (void *)workers.sdramImgOut,
						   (void *)resImgBuf, DMA_WRITE, workers.wImg);
			if(dmaTID==0)
				io_printf(IO_BUF, "[Edging] DMA full for tag-0x%x! Retry...\n", dmatag);
		} while(dmaTID==0);


		// move to the next line
		workers.sdramImgIn += workers.wImg;
		workers.sdramImgOut += workers.wImg;

	} // end for l-loop

	perf.tCore = READ_TIMER();

	// at the end, send MCPL_EDGE_DONE to local leadAp (including the leadAp itself)
#if (DEBUG_LEVEL > 0)
	io_printf(IO_BUF, "[Edging] Done! send MCPL_EDGE_DONE!\n");
#endif
	spin1_send_mc_packet(MCPL_EDGE_DONE, perf.tCore, WITH_PAYLOAD);

}










/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/
/*---------------------------- About Sending Result -----------------------------------*/
/*------------------------ main function: sendResult() --------------------------------*/

// we cannot use workers.imgROut, because workers.imgROut differs from core to core
// use workers.blkImgROut instead!
// nodeID is the next node that is supposed to send the result
// NOTE: in this version, we'll send the gray image. Refer to pA for rgb version!
// The parameter arg1 is used for delay when sending the output to FPGA.
// Hence, in normal operation (send the result to host-PC), arg1 should be 0.
#if(DESTINATION==DEST_HOST)
void sendResultToTargetFromRoot()
{

#if (DEBUG_LEVEL > 0)
	io_printf(IO_STD, "Block-%d is sending with perf = %u\n",
			  blkInfo->nodeBlockID, perf.tNode);
#endif

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
	*/

	uchar *imgOut;
	imgOut  = (uchar *)getSdramResultAddr();

	// use full sdp (scp_segment + data_segment) for pixel values
	ushort rem, sz;

	uint dmatag = DMA_FETCH_IMG_TAG | (myCoreID << 16);
	for(ushort lines=workers.blkStart; lines<=workers.blkEnd; lines++) {
		// get the line from sdram
		//imgOut += l*workers.wImg;

		dmaImgFromSDRAMdone = 0;	// will be altered in hDMA
		do {
			dmaTID = spin1_dma_transfer(dmatag, (void *)imgOut,
										(void *)resImgBuf, DMA_READ, workers.wImg);
		} while(dmaTID==0);

		// wait until dma is completed
		while(dmaImgFromSDRAMdone==0) {
		}

		// then sequentially copy & send via sdp. We use srce_addr and srce_port as well:
		// scre_addr contains line number
		// srce_port contains pixel chunk number in the current line
		uchar chunkID = 0;
		rem = workers.wImg;
		resultMsg.srce_addr = lines;	// is it useful??? for debugging!!!
		uchar *resPtr = resImgBuf;
		do {
			resultMsg.srce_port = chunkID;	// is it useful???
			sz = rem > 272 ? 272 : rem;
			spin1_memcpy((void *)&resultMsg.cmd_rc, resPtr, sz);

			resultMsg.length = sizeof(sdp_hdr_t) + sz;
			spin1_send_sdp_msg(&resultMsg, 10);
			giveDelay(DEF_DEL_VAL);	// this should produce 5.7MBps in 200MHz

			chunkID++;		// for the resImgBuf pointer
			resPtr += sz;
			rem -= sz;
		} while(rem > 0);

		// move to the next address
		imgOut += workers.wImg;

	}

}
#endif

#if(DESTINATION==DEST_FPGA)
// will use fixed-route packet
void sendResultToTargetFromRoot()
{
	// Note: the SpiNN-link is connected to chip<0,0>
	uchar *imgOut;
	imgOut  = (uchar *)getSdramResultAddr();

	// prepare FR packet:
	TODO... lihat kode dari Gengting

	uint dmatag = DMA_FETCH_IMG_TAG | (myCoreID << 16);
	for(ushort lines=workers.blkStart; lines<=workers.blkEnd; lines++) {
		// get the line from sdram
		//imgOut += l*workers.wImg;

		dmaImgFromSDRAMdone = 0;	// will be altered in hDMA
		do {
			dmaTID = spin1_dma_transfer(dmatag, (void *)imgOut,
										(void *)resImgBuf, DMA_READ, workers.wImg);
		} while(dmaTID==0);

		// wait until dma is completed
		while(dmaImgFromSDRAMdone==0) {
		}

		// then sequentially copy & send via fr
		uint frBuf;
		for(ushort px=0; px<workers.wImg; px++) {
			// TODO: lihat kode yang di Gengting....
		}

		// move to the next address
		imgOut += workers.wImg;

	}

}
#endif




/*----------------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------------*/
//             The following is for processing data from other nodes:
#if(DESTINATION==DEST_HOST)
void sendResultToTarget(uint line, uint null)
{

}
#endif

#if(DESTINATION==DEST_FPGA)
void sendResultToTarget(uint line, uint null)
{

}
#endif
/*----------------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------------*/

/* SYNOPSIS:
 * sendResult() will ONLY be executed by leadAp in the root-node.
 *
 * It is called by triggerProcessing() with payload "0".
 *
 * In this version, the root-node will collect the pixel from other nodes, assemble
 * the sdp, and send to the target (host-pc or fpga).
 *
 * It is a chain mechanism, where at the bottom end of it will send again
 * MCPL_BCAST_SEND_RESULT with next blkId as the payload. Hence, which node is
 * supposed to send the result is indicated by this blkID.
 * */
void sendResult(uint blkID, uint arg1)
{	
	// as the root, we have the first chance to send the result
	sendResultToTargetFromRoot();

	// then the rest
	uchar cont;
	for(uint l=workers.endLine+1; l<workers.hImg; l++) {
		cont = 0;
		spin1_send_mc_packet(MCPL_SEND_PIXELS_CMD, l, WITH_PAYLOAD);
		// wait until
		while(cont==0){
		}
	}

	/*
	// if I'm not include in the list, skip this
	if(blkInfo->maxBlock==0) return;

	// special case: if I'm the root and the blkID==maxBlock, then
	// notify host that the sending result process is complete
	if(blkID==blkInfo->maxBlock && blkInfo->nodeBlockID==0) {
		notifyDestDone(0,0);
	} else {
		// check if this is our turn
		if(blkID != blkInfo->nodeBlockID) return;

#if (DESTINATION==DEST_HOST)
		sendResult2Host();
#elif (DESTINATION==DEST_FPGA)
		sendResult2FPGA();
#endif

		// finally, trigger MCPL_BCAST_SEND_RESULT with the next blkID
		spin1_send_mc_packet(MCPL_BCAST_SEND_RESULT, blkInfo->nodeBlockID+1, WITH_PAYLOAD);
	}
	*/
}


// sendResultProcessCmd() will be called when we receive MCPL_SEND_PIXELS_CMD
// from root-node
void sendResultProcessCmd(uint line, uint null)
{
	// first, check if the line is within our block
	if(line>=workers.blkStart && line<=workers.blkEnd) {

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
		do {
			spin1_memcpy((void *)&mcplBuf, resPtr, 4);
			spin1_send_mc_packet(MCPL_SEND_PIXELS_DATA, mcplBuf, WITH_PAYLOAD);

			resPtr += 4;
			rem -= 4;
		} while(rem > 0);
	}
}

void sendResultAssembleLine(uint line, uint null)
{

}

/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/



// notifyDestDone() is executed by <0,0,leadAp> only
void notifyDestDone(uint arg0, uint arg1)
{
	perf.tTotal /= blkInfo->maxBlock;
#if (DEBUG_LEVEL > 0)
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











// computeHist() will use data in the ypxbuf
void computeHist(uint arg0, uint arg1)
{
	for(ushort i=0; i<pxBuffer.pxLen; i++) {
		hist[ypxbuf[i]]++;
	}
}






