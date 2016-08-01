// All distributed processing mechanisms are in this file
#include "SpiNNVid.h"
#include <stdlib.h>	// need for abs()


void initIDcollection(uint withBlkInfo, uint Unused)
{
#if(DEBUG_LEVEL > 0)
	io_printf(IO_BUF, "initIDcollection...\n");
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
	//io_printf(IO_BUF, "Computing workload...\n");
	// keep local (for each core) and to speed up
	ushort w = blkInfo->wImg; workers.wImg = w;
	ushort h = blkInfo->hImg; workers.hImg = h;

	workers.opFilter = blkInfo->opFilter;
	workers.opType = blkInfo->opType;

	// block-wide
	workers.nLinesPerBlock = h / blkInfo->maxBlock;
	ushort nRemInBlock = h % blkInfo->maxBlock;
	workers.blkStart = blkInfo->nodeBlockID * workers.nLinesPerBlock;

	// core-wide
	if(blkInfo->nodeBlockID==blkInfo->maxBlock-1)
		workers.nLinesPerBlock += nRemInBlock;
	workers.blkEnd = workers.blkStart + workers.nLinesPerBlock - 1;

	ushort nLinesPerCore = workers.nLinesPerBlock / workers.tAvailable;
	ushort nRemInCore = workers.nLinesPerBlock % workers.tAvailable;
	ushort wl[17], sp[17], ep[17];	// assuming 17 cores at max
	ushort i,j;

	// initialize starting point with respect to blkStart
	for(i=0; i<17; i++)
		sp[i] = workers.blkStart;

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

	// then align the internal/worker pointer accordingly
	workers.imgRIn = blkInfo->imgRIn + w*workers.startLine;
	workers.imgGIn = blkInfo->imgGIn + w*workers.startLine;
	workers.imgBIn = blkInfo->imgBIn + w*workers.startLine;
	workers.imgOut1 = blkInfo->imgOut1 + w*workers.startLine;
	workers.imgOut2 = blkInfo->imgOut2 + w*workers.startLine;
	workers.imgOut3 = blkInfo->imgOut3 + w*workers.startLine;
	// so, each work has different value of those workers.img*

	// leadAp needs to know, the address of image block
	// it will be used for sending result to host-PC
	if(leadAp) {
		uint szBlk = (workers.blkEnd - workers.blkStart + 1) * workers.wImg;
		uint offset = blkInfo->nodeBlockID * szBlk;
		workers.blkImgRIn = blkInfo->imgRIn + offset;
		workers.blkImgGIn = blkInfo->imgGIn + offset;
		workers.blkImgBIn = blkInfo->imgBIn + offset;
		workers.blkImgOut1 = blkInfo->imgOut1 + offset;
		workers.blkImgOut2 = blkInfo->imgOut2 + offset;
		workers.blkImgOut3 = blkInfo->imgOut3 + offset;
	}

	// prepare the pixel buffers
	ushort szMask = blkInfo->opType == IMG_SOBEL ? 3:5;
	workers.cntPixel = szMask * w;

	// when first called, dtcmImgBuf should be NULL
	if(dtcmImgBuf != NULL) {
		sark_free(dtcmImgBuf);
		sark_free(resImgBuf);
	}
	// dtcmImgBuf can be 3 or 5 times the image width
	dtcmImgBuf = sark_alloc(workers.cntPixel, sizeof(uchar));
	resImgBuf = sark_alloc(w, sizeof(uchar));	// just one line!
	//io_printf(IO_BUF, "my startline = %d, my endline = %d\n", workers.startLine, workers.endLine);
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
	//uint offset = pxBuffer.pxSeq*pxBuffer.pxLen;
	// uint offset = pxBuffer.pxSeq*270;
	uint offset = pxBuffer.pxSeq*DEF_PXLEN_IN_CHUNK;

	// at this point, although workers.imgRIn... workers.imgOut1... have been initialized,
	// we cannot use for storing original image to sdram, since the image distribution
	// is independent of wID !!! So, we have to use blkInfo, which is global for all cores
	// in the chip (it IS independent of wID)

	// coba dengan manual copy: hasilnya? SEMPURNA !!!!
	sark_mem_cpy((void *)blkInfo->imgRIn+offset, (void *)rpxbuf, pxBuffer.pxLen);
	sark_mem_cpy((void *)blkInfo->imgGIn+offset, (void *)gpxbuf, pxBuffer.pxLen);
	sark_mem_cpy((void *)blkInfo->imgBIn+offset, (void *)bpxbuf, pxBuffer.pxLen);
	sark_mem_cpy((void *)blkInfo->imgOut1+offset, (void *)ypxbuf, pxBuffer.pxLen);

	/*
	// the combination of dma and direct memory copying
	uint dmaDone;
	do {
		dmaDone = spin1_dma_transfer(DMA_TAG_STORE_R_PIXELS+myCoreID,
									 blkInfo->imgRIn+offset,
									 rpxbuf, DMA_WRITE, pxBuffer.pxLen);
		//if(dmaDone != 0) io_printf(IO_STD, "dma full!\n");
		if(dmaDone != 0) {
			sark_mem_cpy((void *)blkInfo->imgRIn+offset, (void *)rpxbuf, pxBuffer.pxLen);
			dmaDone = 1;
		}
	} while(dmaDone==0);
	do {
		dmaDone = spin1_dma_transfer(DMA_TAG_STORE_G_PIXELS+myCoreID,
									 blkInfo->imgGIn+offset,
									 gpxbuf, DMA_WRITE, pxBuffer.pxLen);
		//if(dmaDone != 0) io_printf(IO_STD, "dma full!\n");
		if(dmaDone != 0) {
			sark_mem_cpy((void *)blkInfo->imgGIn+offset, (void *)gpxbuf, pxBuffer.pxLen);
			dmaDone = 1;
		}
	} while(dmaDone==0);
	do {
		dmaDone = spin1_dma_transfer(DMA_TAG_STORE_B_PIXELS+myCoreID,
									 blkInfo->imgBIn+offset,
									 bpxbuf, DMA_WRITE, pxBuffer.pxLen);
		//if(dmaDone != 0) io_printf(IO_STD, "dma full!\n");
		if(dmaDone != 0) {
			sark_mem_cpy((void *)blkInfo->imgBIn+offset, (void *)bpxbuf, pxBuffer.pxLen);
			dmaDone = 1;
		}
	} while(dmaDone==0);
	do {
		dmaDone = spin1_dma_transfer(DMA_TAG_STORE_Y_PIXELS+myCoreID,
									 blkInfo->imgOut1+offset,
									 ypxbuf, DMA_WRITE, pxBuffer.pxLen);
		//if(dmaDone != 0) io_printf(IO_STD, "dma full!\n");
		if(dmaDone != 0) {
			sark_mem_cpy((void *)blkInfo->imgOut1+offset, (void *)ypxbuf, pxBuffer.pxLen);
			dmaDone = 1;
		}
	} while(dmaDone==0);
	// Note: jangan broadcast gray pixel, tapi untuk masing-masing channel dan biarkan
	//       core tujuan yang menghitung gray-scale nya sendiri !!!!!
	*/


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

	// then forward the pixels to similar cores but in other chips
	if(sv->p2p_addr==0)
		spin1_schedule_callback(fwdImgData,0,0,PRIORITY_PROCESSING);
}

// once the core receive chunks for all channels, it forwards them
void fwdImgData(uint arg0, uint arg1)
{
	uint payload, i, cntr, szpx, remaining, ch;
	payload = (pxBuffer.pxLen << 16) + pxBuffer.pxSeq;
	cntr = (pxBuffer.pxLen % 4) == 0 ? pxBuffer.pxLen / 4 : (pxBuffer.pxLen / 4) + 1;

	// first forward chunk information
	spin1_send_mc_packet(MCPL_FWD_PIXEL_INFO+myCoreID, payload, WITH_PAYLOAD);

#if (FWD_FULL_COLOR==TRUE)
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

#endif
	/*-------------- gray pixels forwarding ----------------*/

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

}


// collectGrayPixels() will be executed by other chips when they
// all gray-pixel packets from root chip have been received
void collectGrayPixels(uint arg0, uint arg1)
{
	uint offset = pxBuffer.pxSeq*DEF_PXLEN_IN_CHUNK;
	// coba dengan manual copy: hasilnya? SEMPURNA !!!!
	sark_mem_cpy((void *)blkInfo->imgOut1+offset, (void *)ypxbuf, pxBuffer.pxLen);
}

/*------------------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------------------*/
/*----------------------------- IMAGE PROCESSING CORE MECHANISMS ---------------------------*/
/*-----------------------------                                  ---------------------------*/
void triggerProcessing(uint arg0, uint arg1)
{
	/*
	if(blkInfo->opFilter==1) {
		// broadcast command for filtering
		nFiltJobDone = 0;
		spin1_send_mc_packet(MCPL_BCAST_CMD_FILT, 0, WITH_PAYLOAD);
	}
	else {
		nEdgeJobDone = 0;
		// broadcast command for detection
		spin1_send_mc_packet(MCPL_BCAST_CMD_DETECT, 0, WITH_PAYLOAD);
	}
	*/

	// debugging: see, how many chips are able to collect the images:
	// myDelay();
	// io_printf(IO_STD, "Image is completely received. Ready for processing!\n");
	// return;

	// prepare measurement
	tic = sv->clock_ms;

	// how many workers/blocks have finished the process?
	nEdgeJobDone = 0;
	nBlockDone = 0;

	if(blkInfo->opFilter==1) {
		proc = PROC_FILTERING;
		imgFiltering(0,0);	// inside imgFiltering, it will then call imgDetection
	}
	else {
		proc = PROC_EDGING;
		imgDetection(0,0);	// go edge detection directly
	}
}

void imgFiltering(uint arg0, uint arg1)
{
	// step-1: do filtering

	// step-2: call imgDetection
}

void imgDetection(uint arg0, uint arg1)
{
	io_printf(IO_BUF, "Start edge detection\n");
	//io_printf(IO_BUF, "Begin edge detection...\n");
	ushort offset = workers.opType == IMG_SOBEL ? 1:2;
	short l,c,n,i,j;
	uchar *sdramImgIn, *sdramImgOut;
	uchar *dtcmLine;	//point to the current image line in the DTCM (not in SDRAM!)
	int sumX, sumY, sumXY;

	uint adj;

	uint dmatag;

	// how many lines this worker has?
	n = workers.endLine - workers.startLine + 1;

	// io_printf(IO_BUF, "n = %d\n", n);

	// prepare the correct line address in sdram
	if(workers.opFilter==IMG_WITHOUT_FILTER) {
		sdramImgIn = workers.imgOut1;
		sdramImgOut = workers.imgOut2;
	} else {
		sdramImgIn = workers.imgOut2;
		sdramImgOut = workers.imgOut3;
	}

	// scan for all lines in the working block
	for(l=0; l<n; l++) {
		// adjust pointer for each line
		adj = l*workers.wImg;
		sdramImgIn += adj;
		sdramImgOut += adj;

		// shift by mask size for fetching via dma
		sdramImgIn -= offset*workers.wImg;

		dmaImgFromSDRAMdone = 0;
		do {
			dmatag = DMA_FETCH_IMG_TAG | (myCoreID << 16);
			dmaTID = spin1_dma_transfer(dmatag, (void *)sdramImgIn,
										  (void *)dtcmImgBuf, DMA_READ, workers.cntPixel);

			if(dmaTID==0)
				io_printf(IO_BUF, "[Edging] DMA full! Retry...\n");
			/*
			else
				io_printf(IO_BUF, "DMA tag-0x%x tid-%d is requested\n", dmatag, dmaTID);
			*/
		} while (dmaTID==0);

		// wait until dma above is completed
		while(dmaImgFromSDRAMdone==0) {
		}

		// point to the current image line in the DTCM (not in SDRAM!)
		dtcmLine = dtcmImgBuf + offset*workers.wImg;	// mind the offset since dtcmImgBuf contains additional data

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
			*(resImgBuf + c) = 255 - (uchar)(sumXY);
			//*(resImgBuf + c) = (uchar)(sumXY);
			//dmaTID = spin1_dma_transfer((myCoreID << 16) + DMA_STORE_IMG_TAG, (void *)sdramImgOut,
			//			   (void *)resImgBuf, DMA_WRITE, workers.wImg);

		} // end for c-loop

		// then copy the resulting line into sdram
		do {
			dmaTID = spin1_dma_transfer((myCoreID << 16) + DMA_STORE_IMG_TAG, (void *)sdramImgOut,
						   (void *)resImgBuf, DMA_WRITE, workers.wImg);
		} while(dmaTID==0);

	} // end for l-loop

    // at the end, send MCPL_EDGE_DONE
    io_printf(IO_BUF, "Done! send MCPL_EDGE_DONE!\n");
    spin1_send_mc_packet(MCPL_EDGE_DONE, 0, WITH_PAYLOAD);
}

// AfterProcessingDone() is managed by the leadAp in each node.
// But in root-node, afterProcessingDone() will trigger sending the result.
void afterProcessingDone(uint arg0, uint arg1)
{
    if(proc==PROC_EDGING) {
        // optional, won't be used in video streaming:
        io_printf(IO_BUF, "Node-%d is done edging in %d-ms!\n", blkInfo->nodeBlockID, elapse);

        // if root-node, trigger to send the result
        if(sv->p2p_addr==0) {
#if (DESTINATION==DEST_HOST)
            spin1_schedule_callback(sendDetectionResult2Host, 0, 0, PRIORITY_PROCESSING);
#elif (DESTINATION==DEST_FPGA)
            spin1_schedule_callback(sendDetectionResult2FPGA, 0, 0, PRIORITY_PROCESSING);
#endif
        }
    }
}

// check: sendResult() will be executed only by leadAp
// we cannot use workers.imgROut, because workers.imgROut differs from core to core
// use workers.blkImgROut instead!
// nodeID is the next node that is supposed to send the result
// NOTE: in this version, we'll send the gray image. Refer to pA for rgb version!
void sendDetectionResult2Host(uint nodeID, uint arg1)
{
	// if I'm not include in the list, skip this
	if(blkInfo->maxBlock==0) return;

	//io_printf(IO_STD, "Expecting processing by node-%d\n", arg0);
	if(nodeID != blkInfo->nodeBlockID) return;
	// format sdp (scp_segment + data_segment):
	// srce_addr = line number
	// so, it relies on udp reliability (packet might be dropped)
	// the host will assemble the line based on srce_addr, so if the packet
	// is dropped, and the next line is sent, then it produces "defect"
	uchar *imgOut;
	ushort rem, sz;
	ushort l,c;

	// Note: dtcmImgBuf has been allocated in computeWLoad(), but it can be 3 or 5 times the image width.
	// For fetching only one line, use resImgBuf instead

	l = 0;	// for the imgXOut pointer
	if(workers.opFilter==IMG_WITHOUT_FILTER)
		imgOut = workers.blkImgOut2;
	else
		imgOut = workers.blkImgOut3;

	for(ushort lines=workers.blkStart; lines<=workers.blkEnd; lines++) {
		// get the line from sdram
		imgOut += l*workers.wImg;

		dmaImgFromSDRAMdone = 0;	// will be altered in hDMA
		do {
			dmaTID = spin1_dma_transfer(DMA_FETCH_IMG_TAG | (myCoreID << 16), (void *)imgOut,
										(void *)resImgBuf, DMA_READ, workers.wImg);
			if(dmaTID==0)
				io_printf(IO_BUF, "[Sending] DMA full! Retry!\n");

		} while(dmaTID==0);
		// wait until dma is completed
		while(dmaImgFromSDRAMdone==0) {
		}
		// then sequentially copy & send via sdp
		c = 0;
		rem = workers.wImg;
		resultMsg.srce_addr = lines;
		do {
			sz = rem > 272 ? 272 : rem;
			spin1_memcpy((void *)&resultMsg.cmd_rc, (void *)(resImgBuf + c*272), sz);
			resultMsg.length = sizeof(sdp_hdr_t) + sz;

			spin1_send_sdp_msg(&resultMsg, 10);
			giveDelay(DEF_DEL_VAL);

			c++;		// for the resImgBuf pointer
			rem -= sz;
		} while(rem > 0);
		l++;
	}

	//io_printf(IO_STD, "Block-%d done!\n", blkInfo->nodeBlockID);
	//io_printf(IO_BUF, "[Sending] pixels [%d,%d,%d] done!\n", total[0], total[1], total[2]);

	// then send notification to chip<0,0> that my part is complete
	spin1_send_mc_packet(MCPL_BLOCK_DONE, blkInfo->nodeBlockID, WITH_PAYLOAD);

}

// TODO: waiting for Gengting...
void sendDetectionResult2FPGA(uint nodeID, uint arg1)
{

}


void notifyDestDone(uint arg0, uint arg1)
{
#if (DESTINATION==DEST_HOST)
	resultMsg.srce_addr = elapse;
	resultMsg.length = sizeof(sdp_hdr_t);
	spin1_send_sdp_msg(&resultMsg, 10);
#elif (DESTINATION==DEST_FPGA)

#endif
}



























/*--------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------*/
/*-------------------- MUSEUM: from previously failed algorithm ------------------------*/
/*                                                                                      */
/* Note: we cannot broadcast the sdp address because IT IS LOCAL DTCM !!!               */
/*                                                                                      */
/*--------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------*/

/*
// TODO: when do you put the chunk data into sdram?

// storeFrame: call dma for dtcmImgBuf and reset its counter
void storeDtcmImgBuf(uint ch, uint Unused)
{
	uchar *dest;
	switch(ch) {
	case 0:
		dest = blkInfo->imgRIn; blkInfo->imgRIn += pixelCntr; break;
	case 1:
		dest = blkInfo->imgGIn; blkInfo->imgGIn += pixelCntr; break;
	case 2:
		dest = blkInfo->imgBIn; blkInfo->imgBIn += pixelCntr; break;
	}

	spin1_dma_transfer(DMA_TAG_STORE_FRAME, dest, dtcmImgBuf, DMA_WRITE, pixelCntr);

	// then adjust/correct the pointer and the counter
	pixelCntr = 0;
}

/*--------------------------------------------------------------------------------------*
/*-------------------- leadAp forward sdp buffer to core 2,3 and 4 ---------------------*
/*                                                                                      *
/* processImgData() is executed by core-2,3 and 4 only in root node as a response to
 * MCPL with keys MCPL_PROCEED_?_IMG_DATA triggered by leadAp after receiving sdp for
 * frames (through specif frame's port
 * *
void processImgData(uint mBox, uint ch) {
    io_printf(IO_STD, "processImgData with ch-%d is triggered\n", ch);

	sdp_msg_t *msg = (sdp_msg_t *)mBox;
	ushort pxLen = msg->length - sizeof(sdp_hdr_t);

	io_printf(IO_BUF, "in process: got mBox at-0x%x with pxLen=%d\n", msg, pxLen);

	return;

	// step-1: copy the content of mBox to local buffer
	uchar pxInfo[272];
	sark_mem_cpy((void *)pxInfo, (void *)&msg->cmd_rc, pxLen);
	io_printf(IO_BUF, "sdp data is copied...\n");



	// step-2: then release mBox
	spin1_msg_free(msg);
	io_printf(IO_BUF, "sdp is release...\n");



	// step-3: copy to dtcmImgBuf and if necessary trigger dma
	sark_mem_cpy((void *)dtcmImgBuf+pixelCntr, pxInfo, pxLen);
	io_printf(IO_BUF, "copied to dtcmImgBuf...\n");
	pixelCntr += pxLen;
	if(pixelCntr==workers.wImg) {
		spin1_schedule_callback(storeDtcmImgBuf, ch, 0, PRIORITY_PROCESSING);
	}
	// then forward to other nodes
	uchar i, cntr;
	uint payload, base_key, key;

	cntr = (pxLen % 4) == 0 ? pxLen / 4 : (pxLen / 4) + 1;

	switch(ch) {
	case 0: base_key = MCPL_FWD_R_IMG_DATA; break;
	case 1: base_key = MCPL_FWD_G_IMG_DATA; break;
	case 2: base_key = MCPL_FWD_B_IMG_DATA; break;
	}

	ushort szpx, remaining = pxLen;
	for(i=0; i<cntr; i++) {
		if(remaining > sizeof(uint))
			szpx = sizeof(uint);
		else
			szpx = remaining;
		sark_mem_cpy((void *)&payload, (void *)pxInfo + i*4, szpx);

		//key = base_key | (cntr << 8) | i; // we have problem with this scheme
											// because we also need to put the pxLen, but where?
											// solution: assuming MCPL never race!!!

		// the key will contains which channel (base_key), total length of current packet (pxLen)
		// and current length of the chunk (szpx)
		key = base_key | (pxLen << 4) | szpx;
		spin1_send_mc_packet(key, payload, WITH_PAYLOAD);

		remaining -= szpx;

		// for debugging, remove in release mode:
		io_printf(IO_BUF, "key-0x%x load-0x%x has been sent!\n", key, payload);
		sark_delay_us(1000);
	}

	// TODO: decompress data
	// decompress(ch);
	// then reset buffer for next delivery
}



/*--------------------------------------------------------------------------------------*
/*------- core 2,3 and 4 in other nodes (out of root node) receive fwd-ed packets ------*
/*                                                                                      *
/* recvFwdImgData() is executed by core-2,3 and 4 in nodes other than root. It processes
 * forwarded packets from core-2,3 and 4 in root node *
void recvFwdImgData(uint pxData, uint pxLenCh)
{
	io_printf(IO_BUF, "recvFwdImgData is triggered\n");
	uchar  ch = pxLenCh >> 16;
	ushort pxLen = (pxLenCh & 0xFFFF) >> 4;
	uchar  szpx = pxLenCh & 0xF;

	sark_mem_cpy((void *)fwdPktBuffer[ch].pxInfo+fwdPktBuffer[ch].pxLen, (void *)&pxData, szpx);

	fwdPktBuffer[ch].pxLen += szpx;

	// all chunks are collected?
	if(fwdPktBuffer[ch].pxLen == pxLen) {

		// for debugging:
		io_printf(IO_BUF, "Core-%d has received the chunk!\n", myCoreID);

		// put to dtcmImgBuf
		sark_mem_cpy((void *)dtcmImgBuf+pixelCntr, (void *)fwdPktBuffer[ch].pxInfo, pxLen);

		pixelCntr += pxLen;

		if(pixelCntr==workers.wImg)
			spin1_schedule_callback(storeDtcmImgBuf, ch, 0, PRIORITY_PROCESSING);

		// TODO: decompress data
		// decompress(ch);
		// reset the counter for next delivery
		fwdPktBuffer[ch].pxLen = 0;
	}
}



void decompress(uchar ch)
{

}

*/

/*--------------------------------------------------------------------------------------*/
/*-------------------- MUSEUM: from previously failed algorithm ------------------------*/
/*--------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------*/
