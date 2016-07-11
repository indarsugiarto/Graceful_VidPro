// All distributed processing mechanisms are in this file
#include "SpiNNVid.h"

void initIDcollection(uint withBlkInfo, uint Unused)
{
#if(DEBUG_LEVEL > 0)
	io_printf(IO_BUF, "initIDcollection...\n");
#endif
	spin1_send_mc_packet(MCPL_BCAST_INFO_KEY, 0, WITH_PAYLOAD);

	// if withBlkInfo is True, then we need to broadcast blkInfo
	// during run time, this is not necessary since blkInfo has the same content
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

// computeWLoad will be executed by all cores in the network
void computeWLoad(uint withReport, uint arg1)
{
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

/*--------------------------------------------------------------------------------------*/
/*-------------------- leadAp forward sdp buffer to core 2,3 and 4 ---------------------*/
void processImgData(uint mBox, uint ch) {
	sdp_msg_t *msg = (sdp_msg_t *)mBox;
	fwdPktBuffer[ch].pxLen = msg->length - sizeof(sdp_hdr_t);

	// first, copy the content of mBox to local buffer
	sark_mem_cpy((void *)fwdPktBuffer[ch].pxInfo, (void *)&msg->cmd_rc, fwdPktBuffer[ch].pxLen);

	// then release mBox
	spin1_msg_free(msg);

	// then forward to other nodes
	uchar i, cntr = fwdPktBuffer[ch].pxLen / 4;
	uint dummy, base_key, key;

	if((fwdPktBuffer[ch].pxLen % 4) != 0) cntr++;
	switch(ch) {
	case 0: base_key = MCPL_FWD_R_IMG_DATA; break;
	case 1: base_key = MCPL_FWD_G_IMG_DATA; break;
	case 2: base_key = MCPL_FWD_B_IMG_DATA; break;
	}

	ushort szpx, remaining = fwdPktBuffer[ch].pxLen;
	for(i=0; i<cntr; i++) {
		if(remaining > sizeof(uint))
			szpx = sizeof(uint);
		else
			szpx = remaining;
		sark_mem_cpy((void *)&dummy, (void *)fwdPktBuffer[ch].pxInfo + i*4, szpx);
		// the key will contains which channel (base_key), total length of current packet (pxLen)
		// and current length of the chunk (szpx)
		key = base_key | (fwdPktBuffer[ch].pxLen < 4) | szpx;
		spin1_send_mc_packet(key, dummy, WITH_PAYLOAD);
		remaining -= szpx;
	}


	// TODO: decompress data
	decompress(ch);
	// then reset buffer for next delivery
	fwdPktBuffer[ch].pxLen = 0;
}

/*--------------------------------------------------------------------------------------*/
/*------- core 2,3 and 4 in other nodes (out of root node) receive fwd-ed packets ------*/
void recvFwdImgData(uint pxData, uint pxLenCh)
{
	uchar ch = pxLenCh >> 16;
	ushort pxLen = (pxLenCh & 0xFFFF) > 4;
	uchar szpx = pxLenCh & 0xF;
	sark_mem_cpy((void *)fwdPktBuffer[ch].pxInfo + fwdPktBuffer[ch].pxLen,
				 (void *)&pxData, szpx);
	fwdPktBuffer[ch].pxLen += szpx;
	if(fwdPktBuffer[ch].pxLen == pxLen) {
		// TODO: decompress data
		decompress(ch);
		// reset the counter for next delivery
		fwdPktBuffer[ch].pxLen = 0;
	}
}

void decompress(uchar ch)
{

}
