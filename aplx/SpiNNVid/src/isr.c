
#include <spin1_api.h>
#include "profiler.h"

/* idle process uses r25 of system controller
 * Each bit in this register indicates the state of the respective ARM968
 * STANDBYWFI (stand-by wait for interrupt) signal, which is active
 * when the CPU is in its low-power sleep mode.
 * (p.76 of datasheet)
 * */


/* NOTE: This interrupt handler was used for exploiting Timer 2
 * Unfortunately, due to problem with spin1_start() and cpu_sleep(),
 * we cannot use it anymore.
 * */
INT_HANDLER hT2 (void)
{
  tc[T2_INT_CLR] = (uint) tc;			// Clear interrupt in timer
  //tc[T2_INT_CLR] = 1;			// Clear interrupt in timer

  io_printf (IO_BUF, "Tick %d\n", ++T2Ticks);	// Write message to buffer

  spin1_schedule_callback(idle, NULL, NULL, PRIORITY_FIQ);

  vic[VIC_VADDR] = (uint) vic;			// Tell VIC we're done
}
