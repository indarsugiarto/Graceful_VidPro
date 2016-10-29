#include "frameIO.h"

/*------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------*/
/*------------------- Sub functions called by main handler ---------------------*/


void hMC(uint key, uint None)
{
	uint key_hdr = key & 0xFFFF0000;
	uint key_arg = key & 0xFFFF;
	if(key_hdr==MCPL_FRAMEIO_FWD_WID) {
		pxFwdr.wID = key_arg;
		// init buffers in DTCM and SDRAM, but not that in the SYSRAM
		pxFwdr.imgBufDTCM = NULL;
		pxFwdr.imgBufSDRAM = NULL;
	}
}

/* Rule: MCPL always contains key_hdr and key_arg
 * */
void hMCPL(uint key, uint pload)
{
	uint key_hdr = key & 0xFFFF0000;
	uint key_arg = key & 0xFFFF;

	// MCPL_FRAMEIO_OP_INFO is broadcasted by LEAD_CORE
	if(key_hdr==MCPL_FRAMEIO_OP_INFO) {
		// if I'm the streamer
		if(myCore==streamerCore) {
			sdpDelayFactorSpin = (pload >> 16) * DEF_DEL_VAL;
		}
		// otherwise, I'm the pxFwdr
		else {
			pxFwdr.withSharpening = pload & 0x3;
		}
		// the profiler and extLink has their own mechanism
	}
	// MCPL_FRAMEIO_SDRAM_BUF_ADDR is sent from LEAD_CORE to pxFwdr and mcplRecv
	else if(key_hdr==MCPL_FRAMEIO_SDRAM_BUF_ADDR) {
		if(key_arg==SDRAM_BUFF_1_ID)
			// pxFwdr needs this address info:
			pxFwdr.imgBufSDRAM = (uchar *)pload;
		else if(key_arg==SDRAM_BUFF_2_ID)
			// mcplRecv and streamer needs this address info:
			mcplRecv.imgBufSDRAM = (uchar *)pload;
	}
	// MCPL_FRAMEIO_SZFRAME is sent from LEAD_CORE to pxFwdr
	else if(key_hdr==MCPL_FRAMEIO_SZFRAME) {
		// at this point, nCorePerPipe must already be valid
		// either pre-defined, or detected (see main.c)
		spin1_schedule_callback(computeWload, pload, 0, PRIORITY_PROCESSING);
	}
	// MCPL_FRAMEIO_SYSRAM_BUF_ADDR is sent from sdpRecv to pxFwdr
	else if(key_hdr==MCPL_FRAMEIO_SYSRAM_BUF_ADDR) {
		// initRouter makes sure that it is delivered from one sdpRecv to one pxFwdr
		pxFwdr.imgBufSYSRAM = (uchar *)pload;
	}
	// MCPL_FRAMEIO_NEWGRAY is sent from sdpRecv to pxFwdr
	else if(key_hdr==MCPL_FRAMEIO_NEWGRAY) {
		// sdpRecv send the size pixel to fetch
		spin1_schedule_callback(fetch_new_graypx, pload, 0, PRIORITY_PROCESSING);
	}
}
