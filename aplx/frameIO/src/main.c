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

	/*  allocate ?pxbuf
	 *	?pxbuf is used to contain a chunk of image pixels (up to 272 pixels),
	 *	initially sent via SDP and later broadcasted using MCPL */
	pxBuffer.rpxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, 1); // for red chunk
	pxBuffer.gpxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, 1); // for green chunk
	pxBuffer.bpxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, 1); // for blue chunk
	pxBuffer.ypxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, 1); // for gray chunk


	/*  dtcmImgBuf and resImgBuf are used in DMA fetch/store
		to speed up, these two buffers are allocated in computeWLoad() */
	// resImgBuf is used to store one image line from DTCM to SDRAM
	resImgBuf = NULL;

	/*----------------------------------------------------------------------------*/
	/*--------------------------- register callbacks -----------------------------*/
	spin1_callback_on(MCPL_PACKET_RECEIVED, hMCPL, PRIORITY_MCPL);
	spin1_callback_on(MC_PACKET_RECEIVED, hMC, PRIORITY_MC);
	spin1_callback_on(DMA_TRANSFER_DONE, hDMA, PRIORITY_DMA);
	spin1_callback_on(SDP_PACKET_RX, hSDP, PRIORITY_SDP);


	/*----------------------------------------------------------------------------*/
	/*------------------------ other initializations -----------------------------*/

	initSDP();

	// Currently, we fix to core-2 as the leader (LEAD_CORE)
	//            since core-1 will be used for the profiler
	if(myCoreID==LEAD_CORE) {

		// only leadAp: prepare chip-level image block information
		blkInfo = sark_xalloc(sv->sysram_heap, sizeof(block_info_t),
							  XALLOC_TAG_BLKINFO, ALLOC_LOCK);
		if(blkInfo==NULL) {
			io_printf(IO_STD, "[FRAMEIO] blkInfo alloc error!\n");
			rt_error(RTE_ABORT);
		}

		// nodeCntr = 0;
		initRouter();
		initIPTag();
	}

	// Enable performance measurement timer
	ENABLE_TIMER();

	spin1_start(SYNC_NOWAIT);
}

