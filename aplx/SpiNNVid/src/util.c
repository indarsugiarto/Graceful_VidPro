#include "SpiNNVid.h"

// leadAp might use get_Nworkers() to count how many cores
// are in active state AND with ID of SpiNNVid. Once it knows,
// leadAp can use it to count the reply packet from workers.
uchar get_Nworkers()
{

}

void printWID(uint None, uint Neno)
{
	io_printf(IO_BUF, "Total workers = %d\n", workers.tAvailable);
	for(uint i=0; i<workers.tAvailable; i++)
		io_printf(IO_BUF, "wID-%d is core-%d\n", i, workers.wID[i]);
}

uchar get_block_id()
{
	uchar N;

#if(USING_SPIN==3)
	N = CHIP_X(sv->p2p_addr) * 2 + CHIP_Y(sv->p2p_addr);
#else
	uchar x[48] = {0,1,2,3,4,
				   0,1,2,3,4,5,
				   0,1,2,3,4,5,6,
				   0,1,2,3,4,5,6,7,
					 1,2,3,4,5,6,7,
					   2,3,4,5,6,7,
						 3,4,5,6,7,
						   4,5,6,7};
	uchar y[48] = {0,0,0,0,0,
				   1,1,1,1,1,1,
				   2,2,2,2,2,2,2,
				   3,3,3,3,3,3,3,3,
					 4,4,4,4,4,4,4,
					   5,5,5,5,5,5,
						 6,6,6,6,6,
						   7,7,7,7};
	for(uchar i=0; i<48; i++) {
		if((CHIP_X(sv->p2p_addr)==x[i]) && (CHIP_Y(sv->p2p_addr)==y[i])) {
			N = i;
			break;
		}
	}
#endif
	return N;
}