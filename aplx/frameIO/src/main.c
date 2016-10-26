#include "frameIO.h"


/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/*--------------------- Starting point for the SpiNNVid ----------------------*/
void c_main()
{
	if(sv->p2p_addr != 0) {
		io_printf(IO_STD, "[FRAMEIO] Wrong chip! Put me in <0,0>\n");
		return;
	}

	myCoreID = sark_core_id();

	// TODO: gracefully allocate, how many cores will be used per pipeline
	// at the moment, let's fix it:
	nCorePerPipe = 5;

#if (DESTINATION==DEST_FPGA)
	if(sv->p2p_addr==0)
		rtr_fr_set(1 << 4);	// send to link-4, where FPGA is connected to
#endif


	/* Only sdpRecv cores need to allocate pxBuffer and sysram Buffer */
	if((myCoreID >= LEAD_CORE) && (myCoreID < (LEAD_CORE+nCorePerPipe))) {
		/*  allocate ?pxbuf
		 *	?pxbuf is used to contain a chunk of image pixels (up to 272 pixels),
		 *	initially sent via SDP and later broadcasted using MCPL */
		pxBuffer.rpxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, 1); // for red chunk
		pxBuffer.gpxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, 1); // for green chunk
		pxBuffer.bpxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, 1); // for blue chunk
		pxBuffer.ypxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, 1); // for gray chunk
		uint memTag = SDPRECV_SYSRAM_ALLOC_TAG | myCoreID;
		pxBuffer.imgBufSYSRAM = sark_xalloc(sv->sysram_heap, 272, memTag, ALLOC_LOCK);
		if(pxBuffer.imgBufSYSRAM==NULL) {
			io_printf(IO_STD, "[sdpRecv] Sysram alloc error!\n"); rt_error(RTE_ABORT);
		} else {
			spin1_schedule_callback(sdpRecv_infom_pxFwdr, (uint)pxBuffer.imgBufSYSRAM,
									0, PRIORITY_PROCESSING);
		}
	}

	/*----------------------------------------------------------------------------*/
	/*--------------------------- register callbacks -----------------------------*/
	spin1_callback_on(MCPL_PACKET_RECEIVED, hMCPL, PRIORITY_MCPL);
	spin1_callback_on(MC_PACKET_RECEIVED, hMC, PRIORITY_MC);
	spin1_callback_on(DMA_TRANSFER_DONE, hDMA, PRIORITY_DMA);
	spin1_callback_on(SDP_PACKET_RX, hSDP, PRIORITY_SDP);


	/*----------------------------------------------------------------------------*/
	/*------------------------ other initializations -----------------------------*/

	initSDP();

	if(myCoreID==LEAD_CORE) {

		initRouter();
		initIPTag();

		// init global SDRAM image buffer
		frameIO_SDRAM_img_buf1 = NULL;
		frameIO_SDRAM_img_buf2 = NULL;

		// then distribute the working id to pxFwdr
		spin1_schedule_callback(distributeWID, 0, 0, PRIORITY_PROCESSING);
	}

	// Enable performance measurement timer
	ENABLE_TIMER();

	// wait until all cores are ready
	spin1_start(SYNC_WAIT);
}

