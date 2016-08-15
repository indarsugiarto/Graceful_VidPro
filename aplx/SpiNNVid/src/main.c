#include "SpiNNVid.h"

extern void initProfiler();

void c_main(void)
{
	// allocate ?pxbuf
	rpxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, sizeof(uchar));
	gpxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, sizeof(uchar));
	bpxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, sizeof(uchar));
	ypxbuf = sark_alloc(DEF_PXLEN_IN_CHUNK, sizeof(uchar));
	dtcmImgBuf = NULL;
	resImgBuf = NULL;
	// by default, we assign NUM_CORES_FOR_BCAST_PIXEL cores for pre-processing
	// (i.e. grayscaling and histogram counting)
	nCoresForPixelPreProc = NUM_CORES_FOR_BCAST_PIXEL;	// can be changed via SDP

	// do sanity check
	initCheck();

	initProfiler();

	myCoreID = sark_core_id();
	spin1_callback_on(MCPL_PACKET_RECEIVED, hMCPL, PRIORITY_MCPL);
	spin1_callback_on(DMA_TRANSFER_DONE, hDMA, PRIORITY_DMA);

	// main revision: all cores in chip<0,0> may receive frames directly via sdp
	spin1_callback_on(SDP_PACKET_RX, hSDP, PRIORITY_SDP);

	initSDP();	// all cores need to send reportMsg dan debugMsg
				// but only leadAp (chip <0,0>) will react on incoming SDP

	// Other initialization
	initOther();	// it initialize: pixelCntr, fwdPktBuffer

	// Let's assume that all cores will be used for processing
	workers.active = TRUE;

	// Currently, we fix to core-1 as the leader
	//            so that we can use core-2, core-3 and core-4 for handling frames
	//if(leadAp) {
	if(myCoreID==1) {

		// TODO: send notification to host, the coreID of leadAP

        // register timer callback for misc. debugging by leadAp
        spin1_set_timer_tick(TIMER_TICK_PERIOD_US);
        spin1_callback_on(TIMER_TICK, hTimer, PRIORITY_TIMER);

		// only leadAp: prepare chip-level image block information
		blkInfo = sark_xalloc(sv->sysram_heap, sizeof(block_info_t),
							  sark_app_id(), ALLOC_LOCK);
		if(blkInfo==NULL) {
			io_printf(IO_BUF, "blkInfo alloc error!\n");
			rt_error(RTE_ABORT);
			spin1_exit(RTE_ABORT);
		}
		else {
            // let's setup basic/default block info:
            // blkInfo->maxBlock = 0;   // the old version, to prevent chip(s)
                                        // to be used for node(s)
			blkInfo->myX = CHIP_X(sv->p2p_addr);
			blkInfo->myY = CHIP_Y(sv->p2p_addr);
			initImage();	// some of blkInfo are initialized there
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

		// initialize leadAp as a worker
		workers.wID[0] = myCoreID;
		workers.tAvailable = 1;
		workers.subBlockID = 0;	// leadAp has task ID-0

		// nodeCntr = 0;
		initRouter();
		initIPTag();

		// then say hello so that we know if we run the correct version
		if(sv->p2p_addr==0) {
#ifdef USE_FIX_NODES
#if (FWD_FULL_COLOR==TRUE)
			io_printf(IO_STD, "[FIX_NODES:FWD_RGB] SpiNNVid-v%d.%d for Spin%d\n",
					  MAJOR_VERSION, MINOR_VERSION, USING_SPIN);
#else
			io_printf(IO_STD, "[FIX_NODES:FWD_GRY] SpiNNVid-v%d.%d for Spin%d\n",
					  MAJOR_VERSION, MINOR_VERSION, USING_SPIN);
#endif
#else
			io_printf(IO_STD, "[CONFIGURABLE] SpiNNVid-v%d.%d for Spin%d\n",
					  MAJOR_VERSION, MINOR_VERSION, USING_SPIN);
#endif
		}

		// wait for other cores to be ready, otherwise they might
		// not respond to first broadcast for requesting wID
		sark_delay_us(500000);

		// then trigger worker-ID collection
		// NOTE: during run time, leadAp may broadcast wID collection for fault-tolerance!

		//give_report(DEBUG_REPORT_NWORKERS, 1);
		// send the ping command, AND followed by broadcasting blkInfo:
		spin1_schedule_callback(initIDcollection, TRUE, 0, PRIORITY_PROCESSING);
		// during run time, blkInfo is not necessary to broadcasted (it has the same content)
	}

	// then for other cores
	else {
#ifdef USE_FIX_NODES
#if (FWD_FULL_COLOR==TRUE)
			io_printf(IO_BUF, "[FIX_NODES:FWD_RGB] SpiNNVid-v%d.%d for Spin%d\n",
					  MAJOR_VERSION, MINOR_VERSION, USING_SPIN);
#else
		io_printf(IO_BUF, "[FIX_NODES:FWD_GRY] SpiNNVid-v%d.%d for Spin%d\n",
				  MAJOR_VERSION, MINOR_VERSION, USING_SPIN);
#endif
#else
			io_printf(IO_BUF, "[CONFIGURABLE] SpiNNVid-v%d.%d for Spin%d\n",
					  MAJOR_VERSION, MINOR_VERSION, USING_SPIN);
#endif
	}

	// Enable performance measurement timer
	ENABLE_TIMER();

	spin1_start(SYNC_NOWAIT);
}

