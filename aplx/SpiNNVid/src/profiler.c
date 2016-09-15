#include "profiler.h"

/* global variables */
uchar myPhyCore;

// reading temperature sensors
uint tempVal[3];							// there are 3 sensors in each chip
uint cpuIdleCntr[18];						// for all cpus
static uint idle_collect_cntr;              // this is for IDLE_COLLECT_PERIOD
REAL maxIdleCntr;
REAL normIdleCntr;

uint avgCPUidle;							// TODO: how to measure it?
float avgCPUload;							// average CPU load / utilization

// PLL and frequency related (for internal purpose):
uint _r20, _r21, _r24;						// the original value of r20, r21, and r24
uint r20, r21, r24, r25;					// the current value of r20, r21 and r24 during *this* experiment
uint _freq;									// ref/original frequency


/* Future TODO:
 * 1. Function to remap physical core to virtual core in case of
 *    replacing a mulfunction core.
 * */


/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*------------------- function implementations -------------------*/

// use timer2 to handle idle processing. Prepare timer2:
void setup_T2_for_idle_proc()
{
    tc[T2_CONTROL] = 0xe2;			// Set up count-down mode
    tc[T2_LOAD] = _freq * IDLE_UPDATE_PERIOD;		// Load time in microsecs

    sark_vic_set (IDLE_VIC_SLOT, TIMER2_INT, 1, idle);	// Set VIC slot 0
}

void stop_T2_for_idle_proc()
{
    tc[T2_CONTROL] = 0x62;
}

/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
/*-------------------- Initialization & Termination --------------------*/
// initProfiler: use Timer-2 and change PLL
// it returns the current SpiNNaker frequency
uint initProfiler()
{

	// get the original PLL configuration and current frequency
	// usually, we got these values (so, it can be an reference value):
	// _r20 = 0x70128
	// _r21 = 0x7011a
	// _r24 = 0x809488a5

	_r20 = sc[SC_PLL1];
	_r21 = sc[SC_PLL2];
	_r24 = sc[SC_CLKMUX];

	// move system AHB and router to PLL-2
	// PLL-1 will be exclusively used for CPU's clock
	changePLL(1);

	// read the current spiNNaker frequency
	_freq = readSpinFreqVal();
#if (DEBUG_LEVEL>1)
	io_printf(IO_STD, "Switch PLL so that all cores use the same source from PLL-1!\n");
	io_printf(IO_STD, "Current frequency = %d-MHz!\n", currentFreq);
#endif

	// initialize idle process counter and stuffs
	for(uint _idleCntr=0; _idleCntr<18; _idleCntr++)
		cpuIdleCntr[_idleCntr] = 0;
	maxIdleCntr = (REAL)IDLE_SAMPLE_PERIOD;
	normIdleCntr = 100.0/maxIdleCntr;


	// setup timer-2 to handle the idle process
	idle_collect_cntr = 0;
	setup_T2_for_idle_proc();

	return _freq;
}

void terminateProfiler(uint freq)
{
	stop_T2_for_idle_proc();

	// put freq into _freq so that it affect changePLL(0)
	_freq = freq;
	changePLL(0);
}


/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
/*----------------------- CPU Performance stuffs ------------------------*/
// getProcUtil() gets the processor utilization in certain format:
// raw, stdfix, or float. Note that the core list is STILL in phys cores,
// app might need to convert it into virt cores.
void getProcUtil(uint idleCntr[18], uchar format)
{
	uchar i;
	if(format==IDLE_RAW_FORMAT) {
		for(i=0; i<18; i++)
			idleCntr[i] = cpuIdleCntr[i];
	}
	else {
		REAL rel;
		float relf;
		for(i=0; i<18; i++) {
			rel = (maxIdleCntr - (REAL)cpuIdleCntr[i]) * normIdleCntr;
			if(format==IDLE_FLOAT_FORMAT) {
				relf = rel;
				sark_mem_cpy((void *)&idleCntr[i], (void *)&relf, sizeof(uint));
			}
			else {
				sark_mem_cpy((void *)&idleCntr[i], (void *)&rel, sizeof(uint));
			}
		}
	}
}

/*
// computeAvgCPUidle() is the old version. Not used here!
void computeAvgCPUidle()
{
	// at this point idle() is not execute, so it is safe to compute average
	// NOTE: here is the way to get virtual cpu from physical one:
	//			sark.virt_cpu = sv->p2v_map[sark.phys_cpu];
	//		 and here is to get physical from virtual one:
	//			sark.phys_cpu = sv->v2p_map[sark.virt_cpu];

	uint totalCntr = 0;
	uchar cntr = 0;
	for(uchar vCPU=0; vCPU<18; vCPU++) {
		if(sv->vcpu_base[vCPU].cpu_state < CPU_STATE_IDLE) {
			totalCntr += cpuIdleCntr[sv->v2p_map[vCPU]];
			cntr++;
		}
	}
	avgCPUidle = totalCntr / cntr;
	uint f = readSpinFreqVal();
	//float mxCntr = (float)getMaxCntrFromFreq(currentFreq);
	uint halfmxCntr = getMaxCntrFromFreq(f) / 2;
	uint diffr = halfmxCntr - avgCPUidle/2;
	avgCPUload = (float)diffr / (float)halfmxCntr;
	avgCPUload *= 100.0;

	// then reset counter
	for(uchar cpu=0; cpu<18; cpu++)
		cpuIdleCntr[cpu] = 0;
}
*/

/*____________________________________________ CPU Performance stuffs ___*/




/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
/*--------------------- Frequency/PLL-related stuffs --------------------*/
// getFreqParams() read from memTable, the value for ms and ns
void getFreqParams(uint f, uint *ms, uint *ns)
{
	uint i;
	for(i=0; i<lnMemTable; i++)
		if(f == (uint)memTable[i][0]) {
			*ms = (uint)memTable[i][1];
			*ns = (uint)memTable[i][2];
			break;
		}
}

/* readSpinFreqVal() read the current frequency parameters (MS1 and NS1) from system
 * register and then read from memTable, the corresponding/expected frequency.
 * It assumes that the value of _dv_ is always 2. */
uint readSpinFreqVal()
{
	uint i, MS1, NS1;
	uint f = sv->cpu_clk;	// if not found in the table, just return sark version
	r20 = sc[SC_PLL1];

	MS1 = (r20 >> 8) & 0x3F;
	NS1 = r20 & 0x3F;

	for(i=0; i<lnMemTable; i++) {
		if(memTable[i][1]==MS1 && memTable[i][2]==NS1) {
			f = memTable[i][0];
			break;
		}
	}
	return f;
}

void changeFreq(uint f)
{
	r20 = sc[SC_PLL1];			// clk sources for cores should always from PLL1

	uint ns, ms;
	getFreqParams(f, &ms, &ns);
	r20 &= 0xFFFFC0C0;			// apply masking at MS and NS
	r20 |= ns;					// apply NS
	r20 |= (ms << 8);			// apply MS
	sc[SC_PLL1] = r20;			// change the value of r20 with the new parameters

	//_freq = f;					// the actual frequency (sark may report differently)
}

/* SYNOPSIS
 * 		changeFreq()
 * IDEA:
 *		The clock frequency of cpus are determined by PLL1. It is better if clock source for System
 * 		AHB and Router is changed to PLL2, so that we can play only with the cpu.
 *		Using my readPLL.aplx, I found that system AHB and router has 133MHz. That's why, we should
 *		change its divisor to 2, so that the frequency of these elements are kept around 130MHz
 *
 * In function changePLL(), the flag in changePLL means:
 * 0: set to original value
 * 1: set to use PLL2 for system AHB and router
 * 2: set system AHB and router to use the same clock as CPUs
*/
void changePLL(uint flag)
{
	// set to original value
	if(flag==0) {
		changeFreq(_freq);	// set back the original frequency
		sc[SC_CLKMUX] = _r24;	// then restore all registers
		sc[SC_PLL1] = _r20;
		sc[SC_PLL2] = _r21;
	}
	// set to use PLL2 for system AHB and router, PLL1 for CPU's clock
	else if(flag==1) {
		r24 = sc[SC_CLKMUX];
		/* Let's change so that System AHB and Router use PLL2. Hence, system
		 * AHB and Router divisor will be changed to 2 instead of 3.*/
		// the System AHB:
		r24 &= 0xFF0FFFFF; //clear "Sdiv" and "Sys"
		r24 |= 0x00600000; //set Sdiv = 2 and "Sys" = 2
		// the Router:
		r24 &= 0xFFF87FFF; //clear "Rdiv" and "Rtr"
		r24 |= 0x00030000; //set Rdiv = 2 and "Rtr" = 2
		// Apply, so that system AHB and Router is set to PLL2
		sc[SC_CLKMUX] = r24;
	}
	// WARNING: system may behave strange if we set system AHB and router
	// to use the same clock as CPUs, especially at higher than 130MHz
	else if(flag==2) {
		r24 = sc[SC_CLKMUX];
		/* Let's change so that System AHB and Router use PLL1.
		 * System AHB and Router divisor will be changed to 2 instead of 3 */
		// the System AHB:
		r24 &= 0xFF0FFFFF; //clear "Sdiv" and "Sys"
		r24 |= 0x00500000; //set Sdiv = 2 and "Sys" = 1
		// the Router:
		r24 &= 0xFFF87FFF; //clear "Rdiv" and "Rtr"
		r24 |= 0x00028000; //set Rdiv = 2 and "Rtr" = 1
		sc[SC_CLKMUX] = r24;
	}
}

/*_____________________________________ Frequency/PLL-related stuffs ___*/



/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
/*----------------------- Temperature Measurement -----------------------*/
// in readTemp(), read the sensors and put the result in tempVal[]
#define MY_CODE			1
#define PATRICK_CODE	2
#define READING_VERSION	MY_CODE	// 1 = mine, 2 = patrick's
void readTemp()
{
#if (READING_VERSION==1)
	uint i, done, S[] = {SC_TS0, SC_TS1, SC_TS2};

	for(i=0; i<3; i++) {
		done = 0;
		// set S-flag to 1 and wait until F-flag change to 1
		sc[S[i]] = 0x80000000;
		do {
			done = sc[S[i]] & 0x01000000;
		} while(!done);
		// turnoff S-flag and read the value
		sc[S[i]] = sc[S[i]] & 0x0FFFFFFF;
		tempVal[i] = sc[S[i]] & 0x00FFFFFF;
	}

#elif (READING_VERSION==2)
	uint k, temp1, temp2, temp3;

	// Start tempearture measurement
	sc[SC_TS0] = 0x80000000;
	// Wait for measurement TS0 to finish
	k = 0;
	while(!(sc[SC_TS0] & (1<<24))) k++;
	// Get value
	temp1 = sc[SC_TS0] & 0x00ffffff;
	// Stop measurement
	sc[SC_TS0] = 0<<31;
	//io_printf(IO_BUF, "k(T1):%d\n", k);

	// Start tempearture measurement
	sc[SC_TS1] = 0x80000000;
	// Wait for measurement TS1 to finish
	k=0;
	while(!(sc[SC_TS1] & (1<<24))) k++;
	// Get value
	temp2 = sc[SC_TS1] & 0x00ffffff;
	// Stop measurement
	sc[SC_TS1] = 0<<31;
	//io_printf(IO_BUF, "k(T2):%d\n", k);

	// Start tempearture measurement
	sc[SC_TS2] = 0x80000000;
	// Wait for measurement TS2 to finish
	k=0;
	while(!(sc[SC_TS2] & (1<<24))) k++;
	// Get value
	temp3 = sc[SC_TS2] & 0x00ffffff;
	// Stop measurement
	sc[SC_TS2] = 0<<31;
	//io_printf(IO_BUF, "k(T3):%d\n\n", k);
	tempVal[0] = temp1;
	tempVal[1] = temp2;
	tempVal[2] = temp3;
#endif
}

/*____________________________________________ Temperature Measurement __*/


/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
/*-------------------------- Other Utilities ---------------------------*/
void disableCPU(uint virtCoreID, uint none)
{
	uchar pCore = sv->v2p_map[virtCoreID];
	uint code = sc[SC_CPU_DIS];
	code &= 0xFFFFF;               // make sure the security field is cleared
	code |= 0x5EC00000;            // set the security field
	code |= (1 << pCore);          // set DISABLED_CORE to '1'
	sc[SC_CPU_DIS] = code;         // write to the register

	// Then reset via r9 to be in a low-power state
	code = sc[SC_SOFT_RST_P];
	code &= 0xFFFFF;               // make sure the security field is cleared
	code |= 0x5EC00000;            // set the security field
	code |= (1 << pCore);          // send pulse to the DISABLED_CORE to '1'
	sc[SC_SOFT_RST_P] = code;
	//sc[SC_HARD_RST_P] = code;		// kok sepertinya ndak ada bedanya dengan sc[SC_SOFT_RST_P]
	sark_delay_us(1000);
	//testing aja (mungkin bisa dihapus nanti...):
	//sc[SC_SOFT_RST_P] &= ~(1 << pCore);

	// Finally, indicate that core is not functional
	sc[SC_CLR_OK] |= (1 << pCore);
	io_printf(IO_STD, "Core-%u is disabled!\n", virtCoreID); sark_delay_us(1000);
}

void enableCPU(uint virtCoreID, uint none)
{
	uchar pCore = sv->v2p_map[virtCoreID];
	uint code = ~(1 << pCore);        // the result will be something like 11111111111111111111111111111011
	code &= sc[SC_CPU_DIS];           // mask the current register
	code &= 0xFFFFF;                  // make sure the security field is cleared
	code |= 0x5EC00000;               // set the security field
	sc[SC_CPU_DIS] = code;            // write to the register

	// Additionally, set CPU OK to indicate that processor is believed to be functional (info from Datasheet)
	sc[SC_SET_OK] |= (1 << pCore);    // switch only pCore-bit

	io_printf(IO_STD, "Core-%u is enabled!\n", virtCoreID);	sark_delay_us(1000);
}

/*__________________________________________________ Other Utilities ____*/



// Handler for the MCPL packets sent to the profiler.
// NOTE: the routing table must be defined in initRouter()
void hMCPL_profiler(uint key, uint payload)
{

}
