/* This special "streamer" is intended just for streaming the result
 * to host-PC via ETH. It should stay at core-17.
 * */

#include "defSpiNNVid.h"
#include "SpiNNVid.h"

void hMCPL_streamer(uint key, uint pl);
void hDMA_streamer(uint tid, uint tag);
void streamout(uint startAddr, uint None);
extern volatile uint giveDelay(uint delVal);

uint sdpDelay = 900;	// save value for initial == 5.7MBps
ushort wImg;
ushort hImg;
sdp_msg_t streamerMsg;
volatile uchar dmaDone;

void c_main()
{
	if(sv->p2p_addr != 0) {
		io_printf(IO_STD, "Invalid chip! Put me in chip<0,0>!\n");
		return;
	}

	if(sark_core_id() != STREAMER_CORE) {
		io_printf(IO_STD, "Invalid core! Put me in core-%d!\n", STREAMER_CORE);
		return;
	}

	// prepare the sdp
	io_printf(IO_BUF, "[STREAMER] Preparing SDP...\n");
	streamerMsg.flags = 0x07;
	streamerMsg.tag = SDP_TAG_RESULT;
	streamerMsg.dest_port = PORT_ETH;
	streamerMsg.dest_addr = sv->eth_addr;

	io_printf(IO_BUF, "[STREAMER] Preparing callbacks...\n");
	spin1_callback_on(MCPL_PACKET_RECEIVED, hMCPL_streamer, PRIORITY_MCPL);
	spin1_callback_on(DMA_TRANSFER_DONE, hDMA_streamer, PRIORITY_DMA);

	io_printf(IO_BUF, "[STREAMER] Running...\n");
	spin1_start(SYNC_NOWAIT);	// timer2-nya jadi ndak jalan!!!
}

/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*------------------- function implementations -------------------*/


void hMCPL_streamer(uint key, uint pl)
{
	uint key_hdr = key & 0xFFFF0000;
	// only response to MCPL_SEND_PIXELS_BLOCK_NOTIFY_IO
	if(key_hdr == MCPL_SEND_PIXELS_BLOCK_GO_STREAMER) {
		spin1_schedule_callback(streamout, pl, NULL, PRIORITY_PROCESSING);
	}
	else if(key_hdr == MCPL_SEND_PIXELS_BLOCK_INFO_STREAMER_SZIMG) {
		hImg = pl & 0xFFFF;
		wImg = pl >> 16;
		io_printf(IO_BUF, "[STREAMER] Got size %d x %d\n", wImg, hImg);
	}
	else if(key_hdr == MCPL_SEND_PIXELS_BLOCK_INFO_STREAMER_DEL) {
		io_printf(IO_BUF, "[STREAMER] sdpDelay = %d\n", pl);
		sdpDelay = pl;
	}
	else {
		io_printf(IO_BUF, "Got key-0x%x, pl-0x%x\n", key,pl);
	}
}

void hDMA_streamer(uint tid, uint tag)
{
	uint key = tag & 0xFFFF;
	uint core = tag >> 16;
#if(DEBUG_LEVEL>2)
	io_printf(IO_BUF, "dma tid-0x%x, tag-0x%x: key==0x%x, core==%d\n",
				  tid, tag, key, core);
#endif
	if(key == DMA_FETCH_IMG_TAG && core == STREAMER_CORE) {
#if(DEBUG_LEVEL>2)
		io_printf(IO_BUF, "Fetching done!\n");
#endif
		dmaDone = TRUE;
	}
}

#if(DESTINATION==DEST_HOST)
void streamout(uint startAddr, uint None)
{
	/*
	Synopsis:
	1. since image memory is continues, we cannot determine line directly.
	   hence, we cannot use srce_port as chunk index anymore (like in the previous version)
	2. for notifying host, use srce_port 0xFE
	   we cannot use 0xFF, because 0xFF is special for ETH

	Idea 4 speed-up: fetch the data from sdram using DMA, BUT put directly in the sdp buffer.
	*/

	// TODO: Other idea: what if we notify the host earlier (before sending the frame)?

	uchar *imgOut = (uchar *)startAddr;
	uint dmaID, dmatag = DMA_FETCH_IMG_TAG | (STREAMER_CORE << 16);
	uchar *ptrBuf = (uchar *)&streamerMsg.cmd_rc;
	uint rem, sz;

	rem = hImg * wImg;

	streamerMsg.srce_port = (SDP_PORT_FRAME_END << 5) | myCoreID;
	streamerMsg.srce_addr = sv->p2p_addr;
	do {
		sz = rem > 272 ? 272 : rem;
#if(DEBUG_LEVEL > 0)
		io_printf(IO_BUF, "[STREAMER] Fetching via dma...\n");
#endif
		dmaDone = FALSE;	// will be altered in hDMA
		do {
			dmaID = spin1_dma_transfer(dmatag, imgOut, ptrBuf, DMA_READ, sz);
		} while(dmaID==0);

		// wait until dma is completed
		while(dmaDone==FALSE) {
		}

#if(DEBUG_LEVEL > 0)
		io_printf(IO_BUF, "[STREAMER] Sending sdp...\n");
#endif
		streamerMsg.length = sizeof(sdp_hdr_t) + sz;
		spin1_send_sdp_msg(&streamerMsg, 10);

		// give a delay
		giveDelay(sdpDelay);

		// move to the next address
		imgOut += sz;
		rem -= sz;

	} while(rem > 0);

#if(DEBUG_LEVEL > 0)
		io_printf(IO_BUF, "[STREAMER] Notify target...\n");
#endif
	// then notify target done
	streamerMsg.srce_port = SDP_SRCE_NOTIFY_PORT;
	streamerMsg.length = sizeof(sdp_hdr_t);
	spin1_send_sdp_msg(&streamerMsg, 10);
#if(DEBUG_LEVEL>0)
	io_printf(IO_BUF, "[STREAMER] Done!\n");
#endif
}
#endif	// if(DESTINATION==DEST_HOST)

#if(DESTINATION==DEST_FPGA)
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

void streamout(uint startAddr, uint None)
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

}
#endif	// if(DESTINATION==DEST_FPGA)

