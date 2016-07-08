#include "SpiNNVid.h"

void hDMA(uint tag, uin tid)
{

}

void hMCPL(uint key, uint payload)
{
	/*----------------- Worker's part ------------------*/
	if(key==MCPL_BCAST_INFO_KEY) {
		// leadAp sends "0" for ping, workers reply with its core
		if(payload==0)
			spin1_send_mc_packet(MCPL_PING_REPLY, myCoreID, WITH_PAYLOAD);
		else
			blkInfo = (block_info_t *)payload;
	}

	/*------------------ LeadAp part -------------------*/
	else if(key==MCPL_PING_REPLY) {
		workers.wID[workers.tAvailable] = payload;
		workers.tAvailable++;

		// if all other cores have been reporting,
		// broadcast their working load ID
		if(workers.tAvailable==blkInfo->Nworkers) {
			spin1_schedule_callback(bcastWID, 0, 0, PRIORITY_PROCESSING);
		}
	}
}

void hSDP(uint mBox, uint port)
{

}
