#include "SpiNNVid.h"


/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/*--------------------- Starting point for the SpiNNVid ----------------------*/
void SpiNNVid_greetings()
{
	char *stream = sv->p2p_addr == 0 ? IO_STD : IO_BUF;
	if(myCoreID != LEAD_CORE) stream = IO_BUF;

	char tgt[8];
	char cfgNode[20];
	char fwdMode[9];
#if(DESTINATION==DEST_FPGA)
	io_printf(tgt, "FPGA");
#else
	io_printf(tgt, "Host-PC");
#endif
#ifdef USE_FIX_NODES
	// deprecated: if using fix number of nodes
	io_printf(cfgNode, "FIXED_NODES");
#else
	// recent version: using configurable nodes
	io_printf(cfgNode, "CONFIGURABLE_NODES");
#endif
#if(FWD_FULL_COLOR==TRUE)
	io_printf(fwdMode, "FWD_RGB");
#else
	io_printf(fwdMode, "FWD_GRAY");
#endif

	io_printf(stream, "%s Version %d.%d for Spin%d\n", signature,
			  MAJOR_VERSION, MINOR_VERSION, USING_SPIN);
	io_printf(stream, "%s Setup with %s, %s, and %s\n", signature,
				  tgt, cfgNode, fwdMode);

#if(DEBUG_LEVEL>2)
	io_printf(stream, "[SpiNNVid] Initialization...\n");
#endif

	// small delay, so all chips can be ready
	sark_delay_us(get_block_id() * 1000);
}

//void SpiNNVid_main()
void c_main()
{
	// do sanity check: check if the board (Spin3 or Spin5) and app-id are correct
	initCheck();

	// create signature
#if(DEBUG_LEVEL==0)
	io_printf(signature, "[SpiNNVid]");
#else
	io_printf(signature, "[SpiNNVid-dbg-%d]", DEBUG_LEVEL);
#endif

	// first thing: am I a profiler or a SpiNNVid?
	myCoreID = sark_core_id();
	myChipID = get_block_id();
	if(myCoreID == PROF_CORE) {
		terminate_SpiNNVid(IO_DBG, "Invalid core for SpiNNVid!\n", RTE_SWERR);
		return;
	}

#if (DESTINATION==DEST_FPGA)
	if(sv->p2p_addr==0)
		rtr_fr_set(1 << 4);	// send to link-4, where FPGA is connected to
#endif

	/*  allocate ?pxbuf
		?pxbuf is used to contain a chunk of image pixels (up to 272 pixels),
		initially sent via SDP and later broadcasted using MCPL */
	rpxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, sizeof(uchar));		// for red chunk
	gpxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, sizeof(uchar));		// for green chunk
	bpxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, sizeof(uchar));		// for blue chunk
	ypxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, sizeof(uchar));		// for gray chunk


	/*  dtcmImgBuf and resImgBuf are used in DMA fetch/store
		to speed up, these two buffers are allocated in computeWLoad() */

	// dtcmImgBuf is used to fetch image lines from SDRAM to DTCM,
	// it can be 3 or 5 times the image width
	dtcmImgBuf = NULL;

	// resImgBuf is used to store one image line from DTCM to SDRAM
	resImgBuf = NULL;

	// dtcmImgFilt is used to fetch image lines from SDRAM to DTCM,
	// which is fixed 5 times the image width
	dtcmImgFilt = NULL;

	/*  pre-processing: grayscaling and histogram counting
		by default, we assign NUM_CORES_FOR_BCAST_PIXEL cores for pre-processing
		(i.e. grayscaling and histogram counting) */
	nCoresForPixelPreProc = NUM_CORES_FOR_BCAST_PIXEL;	// can be changed via SDP

	/*  newImageFlag will be set on in the beginning or when the host send something
		via SDP_PORT_FRAME_END, and it will be set off when any pixel arrives.
		newImageFlag will be usedful to indicate if the new image just arrives,
		for example	for computing the histogram */
	newImageFlag = TRUE;

	// Let's assume that all cores will be used for processing
	workers.active = TRUE;

	/*----------------------------------------------------------------------------*/
	/*--------------------------- register callbacks -----------------------------*/
	spin1_callback_on(MCPL_PACKET_RECEIVED, hMCPL_SpiNNVid, PRIORITY_MCPL);
	spin1_callback_on(MC_PACKET_RECEIVED, hMC_SpiNNVid, PRIORITY_MC);
	spin1_callback_on(DMA_TRANSFER_DONE, hDMA, PRIORITY_DMA);

	// main revision: all cores in chip<0,0> may receive frames directly via sdp
	spin1_callback_on(SDP_PACKET_RX, hSDP, PRIORITY_SDP);


	/*----------------------------------------------------------------------------*/
	/*------------------------ other initializations -----------------------------*/

	initSDP();	// all cores need to send reportMsg dan debugMsg
				// but only core-2 (in chip <0,0>) will react on incoming SDP

	// Other initialization
	initOther();	// it initialize: pixelCntr, fwdPktBuffer


	// Currently, we fix to core-2 as the leader (LEAD_CORE)
	//            since core-1 will be used for the profiler
	if(myCoreID==LEAD_CORE) {

		// In this SpiNNVid, timer callback is for misc. debugging
        spin1_set_timer_tick(TIMER_TICK_PERIOD_US);
        spin1_callback_on(TIMER_TICK, hTimer, PRIORITY_TIMER);

		// only leadAp: prepare chip-level image block information
		blkInfo = sark_xalloc(sv->sysram_heap, sizeof(block_info_t),
							  XALLOC_TAG_BLKINFO, ALLOC_LOCK);
		if(blkInfo==NULL) {
			terminate_SpiNNVid(IO_DBG, "[FATAL] blkInfo alloc error!\n", RTE_ABORT);
		}
		else {
			/* let's setup basic/default block info: */
			blkInfo->myX = CHIP_X(sv->p2p_addr);
			blkInfo->myY = CHIP_Y(sv->p2p_addr);
			initImgBufs();

			blkInfo->dmaToken_pxStore = LEAD_CORE; // the next core to have dma token is LEAD_CORE

// deprecated: in old version, we use spin3, hence the number of nodes are fix
#ifdef USE_FIX_NODES
			// all chips (including the root) will be given a specific ID:
			blkInfo->maxBlock = get_def_Nblocks();
			blkInfo->nodeBlockID = get_block_id();
#else
			// then we need to wait configuration via SDP
			blkInfo->maxBlock = 0;		// hence, unused chip will be disabled
			blkInfo->nodeBlockID = 255;
#endif
		}

		// initialize leadCore as a worker
		workers.wID[0] = myCoreID;
		workers.tAvailable = 1;
		workers.subBlockID = 0;	// leadAp has task ID-0

		// nodeCntr = 0;
		initRouter();
		initIPTag();

		// then say hello so that we know if we run the correct version
		SpiNNVid_greetings();

		// wait for other cores to be ready, otherwise they might
		// not respond to first broadcast for requesting wID
		sark_delay_us(50000);

		// then trigger worker-ID collection
		// NOTE: during run time, leadCore may broadcast wID collection for fault-tolerance!

		// send the ping command, AND followed by broadcasting blkInfo:
		spin1_schedule_callback(initIDcollection, TRUE, 0, PRIORITY_PROCESSING);
		// during run time, blkInfo is not necessary to broadcasted (it has the same content)
	}

	// then for other cores
	else {
		SpiNNVid_greetings();
	}

	// Enable performance measurement timer
	ENABLE_TIMER();

	spin1_start(SYNC_NOWAIT);
}


/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/*-------------------------- Graceful Termination ----------------------------*/
void terminate_SpiNNVid(char *stream, char *msg, uint exitCode)
{
	char *strm;

	if(msg != NULL) {
		// print message
		// we use IO_DBG to do the trick: if this is chip<0,0>, then change IO_DBG to IO_STD
		// else, change IO_DBG to IO_BUF
		if(stream==IO_DBG) {
			strm = sv->p2p_addr == 0 ? IO_STD : IO_BUF;
		}
		io_printf(strm, msg);
	}

	// THINK/TODO: misc. things:
	// 1. broadcast to all nodes
	// 2. notify host
	// 3. clean up memory

	if(blkInfo != NULL) {

	}
	if(blkInfo->imgRIn != NULL) {

	}
	if(dtcmImgBuf != NULL) {

	}

	// TODO: then tell profiler to terminate as well (by sending an MCPL)

	// finally, exit the spinnaker API
	spin1_exit(exitCode);
}
