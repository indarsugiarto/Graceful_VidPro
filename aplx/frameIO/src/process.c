// All distributed processing mechanisms are in this file
#include "frameIO.h"
#include <stdlib.h>	// need for abs()

/* processGrayScaling() is called when the blue-channel chunk is received. Assuming
 * that the other channels' chunks are received as well.*/
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
	REAL v1,v2,v3;
	uchar grVal;
	for(ushort i=0; i<pxBuffer.pxLen; i++) {
		v1 = (REAL)pxBuffer.rpxbuf[i];
		v2 = (REAL)pxBuffer.gpxbuf[i];
		v3 = (REAL)pxBuffer.bpxbuf[i];

		tmp = roundr(v1*R_GRAY + v2*G_GRAY + v3*B_GRAY);
		// why round first? because if not, it truncate to lower integer

		grVal = (uchar)tmp;

		//pxBuffer.ypxbuf[i] = grVal;
		ypxbuf[i] = grVal;
	}

	// then broadcast the pixels to similar cores but in other chips
	uint payload, i, cntr, szpx, remaining;

	// first forward chunk information
	// payload contains how many pixels and the current chunk-ID
	payload = (pxBuffer.pxLen << 16) + pxBuffer.pxSeq;
	cntr = (pxBuffer.pxLen % 4) == 0 ? pxBuffer.pxLen / 4 : (pxBuffer.pxLen / 4) + 1;

	spin1_send_mc_packet(MCPL_FWD_PIXEL_INFO+myCoreID, payload, WITH_PAYLOAD);

	// then the pixels...
	remaining = pxBuffer.pxLen;
	for(i=0; i<cntr; i++) {
		szpx = remaining > 4 ? 4 : remaining;
		sark_mem_cpy(&payload, pxBuffer.ypxbuf + i*4, szpx);
		spin1_send_mc_packet(MCPL_FWD_PIXEL_YDATA+myCoreID, payload, WITH_PAYLOAD);
		remaining -= szpx;
	}

	// then, send info end-of-forwarding
	spin1_send_mc_packet(MCPL_FWD_PIXEL_EOF+myCoreID, 0, WITH_PAYLOAD);


	// TODO: histogram counting

}

















































void allocateDtcmImgBuf()
{
	if(resImgBuf != NULL) {
		sark_free(resImgBuf); //resImgBuf = NULL;
	}
#if(DEBUG_LEVEL>3)
		io_printf(IO_BUF, "[IMGBUF] Allocating DTCM heap...\n");
#endif
	// prepare the pixel buffers
	ushort szMask = blkInfo->opType == IMG_SOBEL ? 3:5;
	workers.szDtcmImgBuf = szMask * workers.wImg;
	workers.szDtcmImgFilt = 5 * workers.wImg;

	// when first called, dtcmImgBuf should be NULL. It is initialized in SpiNNVid_main()
	/*
	if(dtcmImgBuf != NULL) {
		io_printf(IO_BUF, "[IMGBUF] Releasing DTCM heap...\n");
		sark_free(dtcmImgBuf);
		sark_free(resImgBuf);
	}
	*/

	/* Initializing image buffers in DTCM
	 * dtcmImgBuf can be 3 or 5 times the image width
	 * dtcmImgFilt is fixed 5 times
	*/
	ushort sz4BResImgBuf = workers.wImg;	// to make sure 4 pixels aligned to avoid corruption
	while(sz4BResImgBuf%4 != 0) {
		sz4BResImgBuf++;
	}
	dtcmImgBuf = sark_alloc(workers.szDtcmImgBuf, sizeof(uchar));
	resImgBuf = sark_alloc(sz4BResImgBuf, sizeof(uchar));	// just "aligned" one line!
	dtcmImgFilt = sark_alloc(workers.szDtcmImgFilt, sizeof(uchar));
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
	ushort skippedLines;

	// for root, it should start at 0 position
	if(sv->p2p_addr==0)	{
		blkStart = 0;
		blkEnd = wl[0] - 1;
	}
	else {
		i = 0;
		skippedLines = 0;
		do {
			skippedLines += wl[i];
			i++;
		} while(i<nodeBlockID);


		blkStart = skippedLines;
		blkEnd = blkStart + wl[nodeBlockID] - 1;

		/*
		blkStart = nodeBlockID*wl[nodeBlockID-1];
		blkEnd = blkStart + wl[nodeBlockID] - 1;
		*/
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

	/*
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
	*/
	// all cores in IOnode needs to know the address of image block
	uint szBlk = (workers.blkEnd - workers.blkStart + 1) * workers.wImg;
	uint offset = blkInfo->nodeBlockID * szBlk;
	workers.blkImgRIn = blkInfo->imgRIn + offset;
	workers.blkImgGIn = blkInfo->imgGIn + offset;
	workers.blkImgBIn = blkInfo->imgBIn + offset;
	workers.blkImgOut1 = blkInfo->imgOut1 + offset;


	// other locally copied data (in DTCM, rather than in SysRam)
	workers.opFilter = blkInfo->opFilter;
	workers.opType = blkInfo->opType;
	workers.opSharpen = blkInfo->opSharpen;

	allocateDtcmImgBuf();

	// IT IS WRONG to put the initHistData() here, because initHistData MUST
	// BE CALLED everytime a new image has arrived. So, we move it to processGrayScaling()

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
	uint dmaSent;
	// via dma or direct copy?
	// I think we don't need to use token here, the processing in root-node must
	// take some time before the next core-broadcasting takes place
	do {
		dmaSent = spin1_dma_transfer(DMA_TAG_STORE_Y_PIXELS+myCoreID,
									 blkInfo->imgOut1+offset,
									 ypxbuf, DMA_WRITE, pxBuffer.pxLen);
	} while(dmaSent==0);

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

