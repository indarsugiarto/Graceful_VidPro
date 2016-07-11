#include "SpiNNVid.h"

void c_main(void)
{
	// do sanity check
	initCheck();

	myCoreID = sark_core_id();
	spin1_callback_on(MCPL_PACKET_RECEIVED, hMCPL, PRIORITY_MCPL);
	spin1_callback_on(DMA_TRANSFER_DONE, hDMA, PRIORITY_DMA);
	initSDP();	// all cores need to send reportMsg dan debugMsg
				// but only leadAp (chip <0,0>) will react on incoming SDP

	if(leadAp) {

        // register timer callback for misc. debugging by leadAp
        spin1_set_timer_tick(TIMER_TICK_PERIOD_US);
        spin1_callback_on(TIMER_TICK, hTimer, PRIORITY_TIMER);

		// only leadAp: prepare chip-level image block information
		blkInfo = sark_xalloc(sv->sysram_heap, sizeof(block_info_t),
							  sark_app_id(), ALLOC_LOCK);
		if(blkInfo==NULL) {
			io_printf(IO_BUF, "blkInfo alloc error!\n");
			rt_error(RTE_ABORT);
		}
		else {
            // let's setup basic/default block info:
            // blkInfo->maxBlock = 0;   // the old version, to prevent chip(s)
                                        // to be used for node(s)
            blkInfo->maxBlock = get_def_Nblocks();
            blkInfo->nodeBlockID = get_block_id();
			initImage();	// some of blkInfo are initialized there
		}

		// only leadAp (chip <0,0> responsible for SDP comm
		spin1_callback_on(SDP_PACKET_RX, hSDP, PRIORITY_SDP);

		// initialize leadAp as a worker
		workers.wID[0] = myCoreID;
		workers.tAvailable = 1;
		workers.subBlockID = 0;	// leadAp has task ID-0

		nodeCntr = 0;
		initRouter();
		initIPTag();

		// then say hello so that we know if we run the correct version
		if(sv->p2p_addr==0) {
			io_printf(IO_STD, "SpiNNVid-v%d.%d for Spin%d\n",
					  MAJOR_VERSION, MINOR_VERSION, USING_SPIN);
		}

		// then trigger worker-ID collection
		// NOTE: during run time, leadAp may broadcast wID collection for fault-tolerance!
		blkInfo->Nworkers = get_Nworkers();
		spin1_schedule_callback(initIDcollection, TRUE, 0, PRIORITY_PROCESSING);

		// wait for other cores to be ready, otherwise they might
		// not respond to first broadcast for requesting wID
		sark_delay_us(500000);
	}
	else {
		io_printf(IO_BUF, "SpiNNVid-v%d.%d for Spin%d\n",
				  MAJOR_VERSION, MINOR_VERSION, USING_SPIN);
	}

	spin1_start(SYNC_NOWAIT);
}

