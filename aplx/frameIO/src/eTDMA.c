#include "frameIO.h"

void hDMA(uint tid, uint tag)
{
	uint id = tag & 0xFFFF0000;
	uint core = tag & 0xFFFF;
	if(key==DMA_STORE_IMG_TAG){
		if(core==myCoreID) {
			pxBuffer.dmaDone = TRUE;
		}
	}
	/*
	uint key = tag & 0xFFFF;
	uint core = tag >> 16;
	if(key == DMA_FETCH_IMG_TAG) {
		if(core == myCoreID) {
			//io_printf(IO_BUF, "Reseting dmaImgFromSDRAMdone for core-%d\n", myCoreID);
			dmaImgFromSDRAMdone = 1;	// so the image processing can continue
		}
	}
	else if(key == DMA_TAG_STORE_R_PIXELS) {
		blkInfo->dmaDone_rpxStore = core;
	}
	else if(key == DMA_TAG_STORE_G_PIXELS) {
		blkInfo->dmaDone_gpxStore = core;
	}
	else if(key == DMA_TAG_STORE_B_PIXELS) {
		blkInfo->dmaDone_bpxStore = core;
	}
	*/
}


