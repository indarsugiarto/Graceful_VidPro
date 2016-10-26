// All distributed processing mechanisms are in this file
#include "frameIO.h"
#include <stdlib.h>	// need for abs()

void distributeWID(uint arg0, uint arg1)
{
	uint key;
	for(uchar i=0; i<nCorePerPipe; i++) {
		key = MCPL_FRAMEIO_FWD_WID | i;
		spin1_send_mc_packet(key, 0, NO_PAYLOAD);
	}
}


void sdpRecv_infom_pxFwdr(uint sysramAddr, uint arg1)
{
	uint key = MCPL_FRAMEIO_SYSRARM_BUF | myCoreID;
	spin1_send_mc_packet(key, sysramAddr, WITH_PAYLOAD);
}

void alloc_pxfwdr_bufs()
{
	// pxFwdr doesn't allocate sdram buffer, it is handled by LEAD_CORE!!!
	if(pxFwdr.imgBufDTCM != NULL)
		sark_free(pxFwdr.imgBufDTCM);
	pxFwdr.imgBufDTCM = sark_alloc(wImg, 1);
	if(pxFwdr.imgBufDTCM==NULL){
		io_printf(IO_STD, "[pxFwdr] DTCM mem alloc error!\n"); rt_error(RTE_ABORT);
	}
}

// LEAD_CORE broadcasts MCPL_FRAMEIO_SZFRAME to pxFwdr, streamer, and extLink
void computeWload(uint szFrame, uint arg1)
{
	hImg = szFrame & 0xFFFF;
	wImg = szFrame >> 16;

	// if I'm pxFwdr, calculate working region
	if(myCoreID != STREAMER_CORE) {

		// keep local (for each core) and to speed up
		ushort i;

		/*--------------------------------------------------------------------------*/
		/*----------- let's distribute the region even-ly accross nodes ------------*/
		ushort wl[MAX_CORE_PER_PIPE], sp[MAX_CORE_PER_PIPE], ep[MAX_CORE_PER_PIPE];
		// first, make "default" block-size. Here wl contains the block-size:
		ushort nLinesPerBlock = hImg / nCorePerPipe;
		ushort nRemInBlock = hImg % nCorePerPipe;
		for(i=0; i<nCorePerPipe; i++) {
			wl[i] = nLinesPerBlock;
		}
		// then distribute the remaining part
		i = 0;
		while(nRemInBlock > 0) {
			wl[i]++; i++; nRemInBlock--;
		}
		// then compute the blkStart according to wl (block-size)
		ushort skippedLines;

		// for root, it should start at 0 position
		if(pxFwdr.wID==0)	{
			pxFwdr.blkStart = 0;
			pxFwdr.blkEnd = wl[0] - 1;
		}
		else {
			i = 0;
			skippedLines = 0;
			do {
				skippedLines += wl[i];
				i++;
			} while(i<pxFwdr.wID);

			pxFwdr.blkStart = skippedLines;
			pxFwdr.blkEnd = pxFwdr.blkStart + wl[pxFwdr.wID] - 1;
		}
		pxFwdr.nLinesPerBlock = wl[pxFwdr.wID];
		//pxFwdr.szBlk = (pxFwdr.blkEnd - pxFwdr.blkStart + 1) * wImg;
		pxFwdr.szBlk = pxFwdr.nLinesPerBlock * wImg;

		// then allocate buffers
		alloc_pxfwdr_bufs();
	}
}


/* processGrayScaling() is called when the blue-channel chunk is received. Assuming
 * that the other channels' chunks are received as well.
 *
 * When gray computation is finish, the sdpRecv core stores it in sysram and tells
 * its pxFwdr partner its address to fetch.
 */
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
		pxBuffer.ypxbuf[i] = grVal;
	}


	// then store ypxbuf in sysram
	uint dmatag = DMA_STORE_IMG_TAG | myCoreID;
	uint dmatid;
	pxBuffer.dmaDone = FALSE;
	do {
		dmatid = spin1_dma_transfer(dmatag, pxBuffer.imgBufSYSRAM, pxBuffer.ypxbuf,
								 DMA_WRITE, pxBuffer.pxLen);
	} while(dmatid==0);

	// then wait until dma complete
	while(pxBuffer.dmaDone==FALSE);

	// then tell its pxFwdr partner how many pixels to fetch it from Sysram
	uint key = MCPL_FRAMEIO_NEWGRAY | myCoreID;
	uint pload = (pxBuffer.pxSeq << 16) | pxBuffer.pxLen;
	spin1_send_mc_packet(key, pload, WITH_PAYLOAD);

	/* At this point, the sdpRecv task is complete....
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
	*/
}

/* fetch_new_graypx() is triggered when sdpRecv tells pxFwdr.
 * pxFwdr will fetch from sysram buffer, do histogram counting if necessary,
 * and put into sdram buffer.
 * */
void fetch_new_graypx(uint pxLen, uint None)
{

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

