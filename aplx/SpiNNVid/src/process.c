// All distributed processing mechanisms are in this file
#include "SpiNNVid.h"

void initIDcollection(uint withBlkInfo, uint Unused)
{
	spin1_send_mc_packet(MCPL_BCAST_INFO_KEY, 0, WITH_PAYLOAD);

	// if withBlkInfo is True, then we need to broadcast blkInfo
	// during run time, this is not necessary since blkInfo has the same content
	if(withBlkInfo==TRUE)
		spin1_send_mc_packet(MCPL_BCAST_INFO_KEY, (uint)blkInfo, WITH_PAYLOAD);
}

// bcastWID() will be called by leadAp if it has received
// all ID from other cores
void bcastWID(uint Unused, uint null)
{
	io_printf(IO_BUF, "[SpiNNVid] Distributing wIDs...\n");
	// payload.high = tAvailable, payload.low = wID
	for(uint i= 1; i<workers.tAvailable; i++)	// excluding leadAp
		spin1_send_mc_packet(workers.wID[i], (workers.tAvailable << 16) + i, WITH_PAYLOAD);
#if(DEBUG_LEVEL > 0)
	printWID(0, 0);
#endif
}
