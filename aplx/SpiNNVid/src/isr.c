
#include <sark.h>
#include "profiler.h"

extern uint cpuIdleCntr[18];
extern uint r25;
extern uint idle_collect_cntr;

/* idle process uses r25 of system controller
 * Each bit in this register indicates the state of the respective ARM968
 * STANDBYWFI (stand-by wait for interrupt) signal, which is active
 * when the CPU is in its low-power sleep mode.
 * (p.76 of datasheet)
 * */

INT_HANDLER idle (void)
{
  tc[T2_INT_CLR] = (uint) tc;			// Clear interrupt in timer

  io_printf (IO_BUF, "Tick %d\n", ++ticks);	// Write message to buffer

  uint _idleCntr;
  r25 = sc[SC_SLEEP];
  for(_idleCntr=0; _idleCntr<18; _idleCntr++)
	cpuIdleCntr[_idleCntr] += (r25 >> _idleCntr) & 1;

  //NOTE: the following doesn't make any sense, since we're always busy!!!
  //myOwnIdleCntr++;

  if(idle_collect_cntr >= IDLE_SAMPLE_PERIOD) {
	for(_idleCntr=0; _idleCntr<18; _idleCntr++)
		cpuIdleCntr[_idleCntr] = 0;
  }
  else {
	idle_collect_cntr++;
  }

  vic[VIC_VADDR] = (uint) vic;			// Tell VIC we're done
}

/*
void idle(uint arg0, uint arg1)
{
	uint _idleCntr;
	r25 = sc[SC_SLEEP];
	for(_idleCntr=0; _idleCntr<18; _idleCntr++)
		cpuIdleCntr[_idleCntr] += (r25 >> _idleCntr) & 1;
	myOwnIdleCntr++; //NOTE: it doesn't make any sense, since we're busy!!!
	spin1_schedule_callback(idle, 0, 0, PRIORITY_IDLE);
}
*/
