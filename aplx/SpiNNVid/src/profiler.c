/* NOTE: since profiler uses a lot xTCM space, we remove it from
 * the main SpiNNVid program structure. Now it has its own main.
 * */

/* Future TODO:
 * 1. Function to remap physical core to virtual core in case of
 *    replacing a mulfunction core.
 * */

#include "profiler.h"


void c_main()
{
	myCoreID = sark_core_id();
	if(myCoreID != PROF_CORE) {
		io_printf(IO_STD, "Invalid core! Put me in core-%d!\n", PROF_CORE);
	} else {
		initProfiler();
		initProfilerSDP();

		/* Result: cannot go 200Mhz
		io_printf(IO_BUF, "[PROFILER] Use 200MHz for AHB and router!\n");
		changePLL(2);
		*/

		// experiment: let's use 250MHz for the root-node:
		/*
		if(sv->p2p_addr==0){
			io_printf(IO_STD, "[PROFILER] Set freq to 235MHz!\n");
			changeFreq(235);
		}
		*/

		/*
		io_printf(IO_STD, "[PROFILER] Set freq to 250MHz!\n");
		changeFreq(250);
		*/

		spin1_schedule_callback(readPLL, 1, 0, PRIORITY_PROCESSING);
		/* Result:
		 * 1 with 240MHz, root-node seems work just fine: BUT just once!
		 *   next image load, the node dies!
		 *   by setting delay factor 500 (in my laptop), we got 8.2MBps with perfect result
		 * 2 with 235MHz, root-node alives and work well!
		 *   by setting delaz factor 500 (in my laptop), we got 8MBps with perfect result
		 *   (with or without edge detection)
		 * */

		/*----------------------------------------------------------------------------*/
		/*--------------------------- register callbacks -----------------------------*/
		spin1_callback_on(MCPL_PACKET_RECEIVED, hMCPL_profiler, PRIORITY_URGENT);
		spin1_callback_on(SDP_PACKET_RX, hSDP_profiler, PRIORITY_SDP);

		spin1_start(SYNC_NOWAIT);	// timer2-nya jadi ndak jalan!!!
		// cpu_sleep();	// nah, kalo pakai timer-2, apakah hMCPL_profiler bisa jalan?
		//TODO: think, timer1 or timer2 ??? maybe we need to use timer-1, because
		//		if we use timer2, we HAVE TO use cpu_sleep()
		//		jika pakai timer1, maka harus ada kompensasi frequensi untuk PERIOD-nya!!!
	}
}

/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*------------------- function implementations -------------------*/


void initProfilerSDP()
{
	// prepare the reply message
	reportMsg.flags = 0x07;	//no reply
	reportMsg.tag = SDP_TAG_PROFILER;
	reportMsg.srce_port = (SDP_PORT_PROFILER_RPT << 5) + myCoreID;
	reportMsg.srce_addr = sv->p2p_addr;
	reportMsg.dest_port = PORT_ETH;
	reportMsg.dest_addr = sv->eth_addr;
	reportMsg.length = sizeof(sdp_hdr_t) + sizeof(cmd_hdr_t) + 18;	// it's fix, 18-cores!
}

void hSDP_profiler(uint mBox, uint port)
{

}

void collectReport()
{
	// read the temperature
	readTemp();

	// send via sdp

	// reset the counters
	uint _idleCntr;
	for(_idleCntr=0; _idleCntr<18; _idleCntr++)
		cpuIdleCntr[_idleCntr] = 0;
	idle_collect_cntr = 0;
}

// use timer2 to handle idle processing. Prepare timer2:
void idle(uint tick, uint arg1)
{
	uint _idleCntr;
	r25 = sc[SC_SLEEP];
	for(_idleCntr=0; _idleCntr<18; _idleCntr++)
		cpuIdleCntr[_idleCntr] += (r25 >> _idleCntr) & 1;

	//NOTE: the following doesn't make any sense, since we're always busy!!!
	//myOwnIdleCntr++;

	if(idle_collect_cntr >= IDLE_COLLECT_PERIOD) {
		collectReport();
	}
	else {
	  idle_collect_cntr++;
	}

	// previous version: repeat the idle process
	//spin1_schedule_callback(idle, 0, 0, PRIORITY_IDLE);
}

void setup_Timer_for_idle_proc()
{
	T2Ticks = 0;
	idle_collect_cntr = 0;

	// correct the PERIOD due to frequency different and then set the callback
	uint p = IDLE_SAMPLE_PERIOD * _freq / 200;

	io_printf(IO_BUF, "[PROFILER] Initializing idle-Timer with period = %d\n", p);

	spin1_set_timer_tick(p);
	spin1_callback_on(TIMER_TICK, idle, PRIORITY_FIQ);	// put to FIQ
	/*
	tc[T2_CONTROL] = 0xe2;			// Set up count-down mode
	tc[T2_LOAD] = _freq * IDLE_UPDATE_PERIOD;		// Load time in microsecs
	sark_vic_set (IDLE_VIC_SLOT, TIMER2_INT, 1, hT2);	// Set VIC slot IDLE_VIC_SLOT
	*/
}

void stop_Timer_for_idle_proc()
{
	io_printf(IO_BUF, "[PROFILER] Stopping Timer interrupt...\n");
	//tc[T2_CONTROL] = 0x62;
	spin1_callback_off(TIMER_TICK);
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
	io_printf(IO_BUF, "[PROFILER] Switch PLL so that all cores use the same source from PLL-1!\n");
	changePLL(1);

	// experiment: router is set to 260MHz or 200MHz -> result: corrupted packets!!!
	//changeRtrFreq(0, 2);
	//changeRtrFreq(1, 1);

	// read the current spiNNaker frequency
	_freq = readSpinFreqVal();
	io_printf(IO_BUF, "[PROFILER] Current frequency = %d-MHz!\n", _freq);

	return _freq;

	// initialize idle process counter and stuffs
	for(uint _idleCntr=0; _idleCntr<18; _idleCntr++)
		cpuIdleCntr[_idleCntr] = 0;
	maxIdleCntr = (REAL)IDLE_SAMPLE_PERIOD;
	normIdleCntr = 100.0/maxIdleCntr;


	// setup the timer to handle the idle process
	// Due to spin1_start() and cpu_sleep() problem, we use Timer-1
	setup_Timer_for_idle_proc();

	return _freq;
}

void terminateProfiler(uint freq)
{
	io_printf(IO_BUF, "[PROFILER] Terminating profiler...\n");
	stop_Timer_for_idle_proc();

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
		 * AHB and Router divisor will be changed to 2 instead of 3.
		 * Mem by default already uses PLL-2*/
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

/* changeRtrFreq() change the freq of the Router.
 * Arguments:
 *   divisor = [0-3] will yield 1-4
 *   src = [0-3], 1 for pll-1, and 2 for pll-2
 *
 * Normally, pll-1 is set to 400Mhz and pll-2 is set to 260MHz
 *
 * Example:
 * - to configure router to use 260MHz from pll-2:
 *   changeRtrFreq(0, 2);
 * - to configure router to use 200Mhz from pll-1:
 *   changeRtrFreq(1, 1);
 * */
void changeRtrFreq(uint divisor, uint src)
{
	r24 = sc[SC_CLKMUX];
	r24 &= 0xFFF87FFF; //clear "Rdiv" and "Rtr"
	r24 |= ((divisor << 17) | (src << 15)); //set Rdiv and Rtr
	// Apply, so that system AHB and Router is set to PLL2
	sc[SC_CLKMUX] = r24;
}

REAL getFreq(uchar sel, uchar dv, uchar MS1, uchar NS1, uchar MS2, uchar NS2)
{
	REAL fSrc, num, denum, _dv_, val;
	_dv_ = dv;
	switch(sel) {
	case 0: num = REAL_CONST(1.0); denum = REAL_CONST(1.0); break; // 10 MHz clk_in
	case 1: num = NS1; denum = MS1; break;
	case 2: num = NS2; denum = MS2; break;
	case 3: num = REAL_CONST(1.0); denum = REAL_CONST(4.0); break;
	}
	fSrc = REAL_CONST(10.0);
	val = (fSrc * num) / (denum * _dv_);
	return val;
}

char *selName(uchar s)
{
	char *name;
	switch(s) {
	case 0: name = "clk_in"; break;
	case 1: name = "pll1_clk"; break;
	case 2: name = "pll2_clk"; break;
	case 3: name = "clk_in_div_4"; break;
	}
	return name;
}

char *get_FR_str(uchar fr)
{
	char *str;
	switch(fr) {
	case 0: str = "25-50 MHz"; break;
	case 1: str = "50-100 MHz"; break;
	case 2: str = "100-200 MHz"; break;
	case 3: str = "200-400 MHz"; break;
	}
	return str;
}


void readPLL(uint chip_addr, uint null)
{
	char *stream = IO_BUF;
	//if(chip_addr==0) stream = IO_STD; else stream = IO_BUF;

	uint r20 = sc[SC_PLL1];
	uint r21 = sc[SC_PLL2];
	uint r24 = sc[SC_CLKMUX];

	uchar FR1, MS1, NS1, FR2, MS2, NS2;
	uchar Sdiv, Sys_sel, Rdiv, Rtr_sel, Mdiv, Mem_sel, Bdiv, Pb, Adiv, Pa;

	FR1 = (r20 >> 16) & 3;
	MS1 = (r20 >> 8) & 0x3F;
	NS1 = r20 & 0x3F;
	FR2 = (r21 >> 16) & 3;
	MS2 = (r21 >> 8) & 0x3F;
	NS2 = r21 & 0x3F;

	Sdiv = ((r24 >> 22) & 3) + 1;
	Sys_sel = (r24 >> 20) & 3;
	Rdiv = ((r24 >> 17) & 3) + 1;
	Rtr_sel = (r24 >> 15) & 3;
	Mdiv = ((r24 >> 12) & 3) + 1;
	Mem_sel = (r24 >> 10) & 3;
	Bdiv = ((r24 >> 7) & 3) + 1;
	Pb = (r24 >> 5) & 3;
	Adiv = ((r24 >> 2) & 3) + 1;
	Pa = r24 & 3;

	REAL Sfreq, Rfreq, Mfreq, Bfreq, Afreq;
	Sfreq = getFreq(Sys_sel, Sdiv, MS1, NS1, MS2, NS2);
	Rfreq = getFreq(Rtr_sel, Rdiv, MS1, NS1, MS2, NS2);
	Mfreq = getFreq(Mem_sel, Mdiv, MS1, NS1, MS2, NS2);
	Bfreq = getFreq(Pb, Bdiv, MS1, NS1, MS2, NS2);
	Afreq = getFreq(Pa, Adiv, MS1, NS1, MS2, NS2);


	// TODO: pindah ke profiler, karena DTCM ndak cukup untuk tulisan-tulisan di bawah ini:

	io_printf(stream, "\n\n************* CLOCK INFORMATION **************\n");
	io_printf(stream, "Reading sark library...\n");
	io_printf(stream, "Clock divisors for system & router bus: %u\n", sv->clk_div);
	io_printf(stream, "CPU clock in MHz   : %u\n", sv->cpu_clk);
	//io_printf(IO_STD, "CPU clock in MHz   : %u\n", sark.cpu_clk); sark_delay_us(1000);
	io_printf(stream, "SDRAM clock in MHz : %u\n\n", sv->mem_clk);

	io_printf(stream, "Reading registers directly...\n");
	io_printf(stream, "PLL-1\n"); sark_delay_us(1000);
	io_printf(stream, "----------------------------\n");
	io_printf(stream, "Frequency range      : %s\n", get_FR_str(FR1));
	io_printf(stream, "Output clk divider   : %u\n", MS1);
	io_printf(stream, "Input clk multiplier : %u\n\n", NS1);

	io_printf(stream, "PLL-2\n");
	io_printf(stream, "----------------------------\n");
	io_printf(stream, "Frequency range      : %s\n", get_FR_str(FR2));
	io_printf(stream, "Output clk divider   : %u\n", MS2);
	io_printf(stream, "Input clk multiplier : %u\n\n", NS2);

	io_printf(stream, "Multiplerxer\n"); sark_delay_us(1000);
	io_printf(stream, "----------------------------\n");
	io_printf(stream, "System AHB clk divisor  : %u\n", Sdiv);
	io_printf(stream, "System AHB clk selector : %u (%s)\n", Sys_sel, selName(Sys_sel));
	io_printf(stream, "System AHB clk freq     : %k MHz\n", Sfreq);
	io_printf(stream, "Router clk divisor      : %u\n", Rdiv);
	io_printf(stream, "Router clk selector     : %u (%s)\n", Rtr_sel, selName(Rtr_sel));
	io_printf(stream, "Router clk freq         : %k MHz\n", Rfreq);
	io_printf(stream, "SDRAM clk divisor       : %u\n", Mdiv);
	io_printf(stream, "SDRAM clk selector      : %u (%s)\n", Mem_sel, selName(Mem_sel));
	io_printf(stream, "SDRAM clk freq          : %k MHz\n", Mfreq);
	io_printf(stream, "CPU-B clk divisor       : %u\n", Bdiv);
	io_printf(stream, "CPU-B clk selector      : %u (%s)\n", Pb, selName(Pb));
	io_printf(stream, "CPU-B clk freq          : %k MHz\n", Bfreq);
	io_printf(stream, "CPU-A clk divisor       : %u\n", Adiv);
	io_printf(stream, "CPU-A clk selector      : %u (%s)\n", Pa, selName(Pa));
	io_printf(stream, "CPU-A clk freq          : %k MHz\n", Afreq);
	io_printf(stream, "**********************************************\n\n\n");
}

/*_____________________________________ Frequency/PLL-related stuffs ___*/



/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
/*----------------------- Temperature Measurement -----------------------*/
// in readTemp(), read the sensors and put the result in tempVal[]
REAL readTemp()
{
	REAL result;
#if (READING_VERSION==1)
	uint i, done, S[] = {SC_TS0, SC_TS1, SC_TS2};

	// in this version, we'll use only sensor-2
	for(i=2; i<3; i++) {
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

	// then convert from uint to REAL value


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

	return result;
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


void setFreq(uint f, uint null)
{
	// if f is set to 0, then we play the adaptive strategy with initial freq 200MHz
	if(f==0) {
		//_freq = 200;	// use whatever current freq on SpiNNaker (read during init)
		adaptiveFreq = TRUE;
		io_printf(IO_BUF, "[PROFILER] Will use the current SpiNN freq!\n");
	} else {
		_freq = f;
		adaptiveFreq = FALSE;
		changeFreq(_freq);
		io_printf(IO_BUF, "[PROFILER] Set freq to %d\n", f);
	}
}



// Handler for the MCPL packets sent to the profiler.
// NOTE: the routing table must be defined in initRouter()
void hMCPL_profiler(uint key, uint payload)
{
	uint pl = payload & 0xFFFF;
	uint arg = payload >> 16;
	if(pl == PROF_MSG_PLL_INFO) {
		readPLL(sv->p2p_addr, 0);
	}
	else if(pl == PROF_MSG_SET_FREQ) {
		setFreq(arg, NULL);
	}
	else if(key == MCPL_TO_OWN_PROFILER) {
		// TODO: if adaptive frequency
	}
}
