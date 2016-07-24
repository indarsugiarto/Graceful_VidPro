// All distributed processing mechanisms are in this file
#include "SpiNNVid.h"


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
	// keep local (for each core)
	ushort w = blkInfo->wImg; workers.wImg = w;
	ushort h = blkInfo->hImg; workers.hImg = h;

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
}




// processGrayScaling() will process pxBuffer
void processGrayScaling(uint arg0, uint arg1)
{
	REAL tmp;
	ushort grVal;
	for(ushort i=0; i<pxBuffer.pxLen; i++) {
		tmp = (REAL)pxBuffer.rpxbuf[i] * R_GRAY +
			  (REAL)pxBuffer.gpxbuf[i] * G_GRAY +
			  (REAL)pxBuffer.bpxbuf[i] * B_GRAY;
		grVal = (ushort)tmp;
		pxBuffer.ypxbuf[i] = grVal>255?255:grVal;
	}
	// then copy to sdram at location pxBuffer.pxSeq
	// Note: blkInfo->imgOut1 must be initialized already and all cores must
	//       know it already (ie. blkInfo must be broadcasted in advance)
	uint dmaDone;
	do {
		dmaDone = spin1_dma_transfer(DMA_TAG_STORE_R_PIXELS, blkInfo->imgRIn+(pxBuffer.pxSeq*270),
					   pxBuffer.rpxbuf, DMA_WRITE, pxBuffer.pxLen);
	} while(dmaDone==0);
	do {
		dmaDone = spin1_dma_transfer(DMA_TAG_STORE_G_PIXELS, blkInfo->imgGIn+(pxBuffer.pxSeq*270),
					   pxBuffer.gpxbuf, DMA_WRITE, pxBuffer.pxLen);
	} while(dmaDone==0);
	do {
		dmaDone = spin1_dma_transfer(DMA_TAG_STORE_B_PIXELS, blkInfo->imgRIn+(pxBuffer.pxSeq*270),
					   pxBuffer.rpxbuf, DMA_WRITE, pxBuffer.pxLen);
	} while(dmaDone==0);
	do {
		dmaDone = spin1_dma_transfer(DMA_TAG_STORE_Y_PIXELS, blkInfo->imgOut1+(pxBuffer.pxSeq*270),
					   pxBuffer.ypxbuf, DMA_WRITE, pxBuffer.pxLen);
	} while(dmaDone==0);

	// TODO: broadcast the pixels to external chips
	// Note: jangan broadcast di sini, tapi untuk masing-masing channel dan biarkan
	//       core tujuan yang menghitung gray-scale nya sendiri !!!!!
}

// in recvFwdImgData(), payload contains 4 color information of a pixel
void recvFwdImgData(uint key, uint payload)
{

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
