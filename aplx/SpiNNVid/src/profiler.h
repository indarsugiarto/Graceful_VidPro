#ifndef PROFILER_H
#define PROFILER_H

/*
 * Note:
 * - This is the simplified version of the profiler (timer2 is not included here).
 * - Regarding timer2, can be seen in the project tryMyTimer.
 * - from https://www.arm.com/products/processors/classic/arm9/arm968.php
 *   it seems that the max frequency is 270MHz
 *
 * CPUs use PLL1
 */
#include <spin1_api.h>
#include "defSpiNNVid.h"
#include <stdfix.h>

#ifndef REAL
#define REAL						accum
#define REAL_CONST(x)				(x##k)
#endif

//#define REPORT_TIMER_TICK_PERIOD_US	1000000	// to get 1s resolution in FREQ_REF_200MHZ
#define FREQ_REF_200MHZ				200

// related with frequency scalling
#define lnMemTable					93
#define wdMemTable					3
// memTable format: freq, MS, NS --> with default dv = 2, so that we don't have
// to modify r24
static uchar memTable[lnMemTable][wdMemTable] = {
{10,1,2},
{11,5,11},
{12,5,12},
{13,5,13},
{14,5,14},
{15,1,3},
{16,5,16},
{17,5,17},
{18,5,18},
{19,5,19},
{20,1,4},
{21,5,21},
{22,5,22},
{23,5,23},
{24,5,24},
{25,1,5},
{26,5,26},
{27,5,27},
{28,5,28},
{29,5,29},
{30,1,6},
{31,5,31},
{32,5,32},
{33,5,33},
{34,5,34},
{35,1,7},
{36,5,36},
{37,5,37},
{38,5,38},
{39,5,39},
{40,1,8},
{41,5,41},
{42,5,42},
{43,5,43},
{44,5,44},
{45,1,9},
{46,5,46},
{47,5,47},
{48,5,48},
{49,5,49},
{50,1,10},
{51,5,51},
{52,5,52},
{53,5,53},
{54,5,54},
{55,1,11},
{56,5,56},
{57,5,57},
{58,5,58},
{59,5,59},
{60,1,12},
{61,5,61},
{62,5,62},
{63,5,63},
{65,1,13},
{70,1,14},
{75,1,15},
{80,1,16},
{85,1,17},
{90,1,18},
{95,1,19},
{100,1,20},
{105,1,21},
{110,1,22},
{115,1,23},
{120,1,24},
{125,1,25},
{130,1,26},
{135,1,27},
{140,1,28},
{145,1,29},
{150,1,30},
{155,1,31},
{160,1,32},
{165,1,33},
{170,1,34},
{175,1,35},
{180,1,36},
{185,1,37},
{190,1,38},
{195,1,39},
{200,1,40},
{205,1,41},
{210,1,42},
{215,1,43},
{220,1,44},
{225,1,45},
{230,1,46},
{235,1,47},
{240,1,48},
{245,1,49},
{250,1,50},
{255,1,51},
};

/* global variables */
uchar myPhyCore;

// reading temperature sensors
uint tempVal[3];							// there are 3 sensors in each chip
uint cpuIdleCntr[18];						// for all cpus
uint myOwnIdleCntr;							// since my flag in r25 is alway on, it gives me ALWAYS zero counts
uint avgCPUidle;							// TODO: how to measure it?
float avgCPUload;							// average CPU load / utilization

// PLL and frequency related (for internal purpose):
uint _r20, _r21, _r24;						// the original value of r20, r21, and r24
uint r20, r21, r24, r25;					// the current value of r20, r21 and r24 during *this* experiment
uint _freq;									// ref/original frequency
uint currentFreq;

/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*----------------------------------------------------------------*/
/*---------------------- function prototypes ---------------------*/
// temperature-related
void readTemp();

// frequency-related
void getFreqParams(uint f, uint *ms, uint *ns);
void changeFreq(uint f);					// we use uchar to limit the frequency to 255
void changePLL(uint flag);
uint readSpinFreqVal();

// cpu-utilisation related
void idle(uint arg0, uint arg1);
void disableCPU(uint virtCoreID, uint none);
void enableCPU(uint virtCoreID, uint none);
void computeAvgCPUidle();

// initProfiler will set timer-2 and PLL
void initProfiler();		// mainly for changing PLL-2
void terminateProfiler();	// and for restoring PLL-2

// the following values are based on manual experiment
typedef struct idle_cntr
{
	uint freq;
	uint cntr;
} idle_cntr_t;
// lnMemTable is used in the profiler.h
idle_cntr_t idle_cntr_table[lnMemTable];

void buildIdleCntrTable();
uint getMaxCntrFromFreq(uint freq);

#endif // PROFILER_H

