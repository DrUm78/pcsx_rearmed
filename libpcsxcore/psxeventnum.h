#ifndef _PSXEVENTNUM_
#define _PSXEVENTNUM_
enum psxEventNum {
	PSXINT_SIO = 0,
	PSXINT_CDR,
	PSXINT_CDREAD,
	PSXINT_GPUDMA,
	PSXINT_MDECOUTDMA,
	PSXINT_SPUDMA,
	PSXINT_GPUBUSY,        //From PCSX Rearmed, but not implemented there nor here
	PSXINT_MDECINDMA,
	PSXINT_GPUOTCDMA,
	PSXINT_CDRDMA,
	PSXINT_NEWDRC_CHECK,   //Used in PCSX Rearmed dynarec
	PSXINT_RCNT,
	PSXINT_CDRLID,
	PSXINT_CDRPLAY,
	PSXINT_SPUIRQ,         //Check for upcoming SPU HW interrupts
	PSXINT_SPU_UPDATE,     //senquack - update and feed SPU (note that this usage
	                       // differs from Rearmed: Rearmed uses this for checking
	                       // for SPU HW interrupts and lacks a flexibly-scheduled
	                       // interval for SPU update-and-feed)
	PSXINT_RESET_CYCLE_VAL,          // Reset psxRegs.cycle value to 0 to ensure
	                                 //  it can never overflow
	PSXINT_SIO_SYNC_MCD,             // Flush/sync/close memcards opened for writing
	PSXINT_COUNT,
	PSXINT_NEXT_EVENT = PSXINT_COUNT //The most imminent event's entry is
	                                 // always copied to this slot in
	                                 // psxRegs.intCycles[] for fast checking
};
#endif
