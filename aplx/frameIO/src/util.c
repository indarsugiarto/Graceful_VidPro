#include "frameIO.h"

// Host might want to know, how many cores are running frameIO app
uchar get_Nworkers()
{
	uchar i, nCores = sv->num_cpus, nApp = 0;
	for(i=0; i<nCores; i++) {
		if(sv->vcpu_base[i].app_id == FRAMEIO_APP_ID) {
			if(sv->vcpu_base[i].cpu_state >= CPU_STATE_RUN &&
			   sv->vcpu_base[i].cpu_state < CPU_STATE_EXIT)
				nApp++;
		}
	}
	//io_printf(IO_STD, "Found %d available workers\n", nApp);
	return nApp;
}

inline REAL roundr(REAL inVal)
{
	uint base = (uint)inVal;
	uint upper = base + 1;
	REAL conver = inVal + REAL_CONST(0.5);
	if((uint)conver == base)
		return (REAL)base;
	else
		return (REAL)upper;
}

volatile uint giveDelay(uint delVal)
{
  volatile uint dummy = delVal;
  volatile uint step = 0;
  while(step < delVal) {
    dummy += (2 * step);
    step++;
  }
  return dummy;
}
