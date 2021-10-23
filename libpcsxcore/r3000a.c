/***************************************************************************
 *   Copyright (C) 2007 Ryan Schultz, PCSX-df Team, PCSX team              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02111-1307 USA.           *
 ***************************************************************************/

/*
* R3000A CPU functions.
*/

#include "r3000a.h"
#include "cdrom.h"
#include "mdec.h"
#include "gte.h"
#include "psxevents.h"

R3000Acpu *psxCpu = NULL;
#ifdef DRC_DISABLE
psxRegisters psxRegs;
#endif

int psxInit() {
	SysPrintf(_("Running PCSX Version %s (%s).\n"), PACKAGE_VERSION, __DATE__);

#ifndef DRC_DISABLE
	if (Config.Cpu == CPU_INTERPRETER) {
		psxCpu = &psxInt;
	} else psxCpu = &psxRec;
#else
	psxCpu = &psxInt;
#endif

	Log = 0;

	if (psxMemInit() == -1) return -1;

	return psxCpu->Init();
}

void psxReset() {
	psxMemReset();

	memset(&psxRegs, 0, sizeof(psxRegs));

	psxRegs.pc = 0xbfc00000; // Start in bootstrap

	psxRegs.CP0.r[12] = 0x10900000; // COP0 enabled | BEV = 1 | TS = 1
	psxRegs.CP0.r[15] = 0x00000002; // PRevID = Revision ID, same as R3000A

	psxCpu->Reset();

	psxEvqueueInit();  // Event scheduler queue
	psxHwReset();
	psxBiosInit();

	if (!Config.HLE)
		psxExecuteBios();

#ifdef EMU_LOG
	EMU_LOG("*BIOS END*\n");
#endif
	Log = 0;
}

void psxShutdown() {
	psxMemShutdown();
	psxBiosShutdown();

	psxCpu->Shutdown();
}

void psxException(u32 code, u32 bd) {
	#ifdef ICACHE_EMULATION
	/* Dynarecs may use this codepath and crash as a result.
	 * This should only be used for the interpreter. - Gameblabla
	 * */
	if (Config.icache_emulation && Config.Cpu == CPU_INTERPRETER)
	{
		psxRegs.code = SWAPu32(*Read_ICache(psxRegs.pc));
	}
	else
	#endif
	{
		psxRegs.code = PSXMu32(psxRegs.pc);
	}

	// Set the Cause
	psxRegs.CP0.n.Cause = (psxRegs.CP0.n.Cause & 0x300) | code;

	// Set the EPC & PC
	if (bd) {
#ifdef PSXCPU_LOG
		PSXCPU_LOG("bd set!!!\n");
#endif
		psxRegs.CP0.n.Cause |= 0x80000000;
		psxRegs.CP0.n.EPC = (psxRegs.pc - 4);
	} else
		psxRegs.CP0.n.EPC = (psxRegs.pc);

	if (psxRegs.CP0.n.Status & 0x400000)
		psxRegs.pc = 0xbfc00180;
	else
		psxRegs.pc = 0x80000080;

	// Set the Status
	psxRegs.CP0.n.Status = (psxRegs.CP0.n.Status &~0x3f) |
						  ((psxRegs.CP0.n.Status & 0xf) << 2);

	if (!Config.HLE && (((PSXMu32(psxRegs.CP0.n.EPC) >> 24) & 0xfe) == 0x4a)) {
		// "hokuto no ken" / "Crash Bandicot 2" ... fix
		PSXMu32ref(psxRegs.CP0.n.EPC)&= SWAPu32(~0x02000000);
	}

	if (Config.HLE) psxBiosException();
}

void psxBranchTest() {
	//senquack - Do not rearrange the math here! Events' sCycle val can end up
	// negative (very large unsigned int) when a PSXINT_RESET_CYCLE_VAL event
	// resets psxRegs.cycle to 0 and subtracts the previous psxRegs.cycle value
	// from each event's sCycle value. If you were instead to test like this:
	// 'while ((psxRegs.cycle >= (psxRegs.intCycle[X].sCycle + psxRegs.intCycle[X].cycle)',
	// it could fail for events that were past-due at the moment of adjustment.
	while ((psxRegs.cycle - psxRegs.intCycle[PSXINT_NEXT_EVENT].sCycle) >=
			psxRegs.intCycle[PSXINT_NEXT_EVENT].cycle) {
		// After dispatching the most-imminent event, this will update
		//  the intCycle[PSXINT_NEXT_EVENT] element.
		psxEvqueueDispatchAndRemoveFront(&psxRegs);
	}

	psxRegs.io_cycle_counter = psxRegs.intCycle[PSXINT_NEXT_EVENT].sCycle +
	                           psxRegs.intCycle[PSXINT_NEXT_EVENT].cycle;

	// Are one or more HW IRQ bits set in both their status and mask registers?
	if (psxHu32(0x1070) & psxHu32(0x1074)) {
		// Are both HW IRQ mask bit and IRQ master-enable bit set in CP0 status reg?
		if ((psxRegs.CP0.n.Status & 0x401) == 0x401) {
			psxException(0x400, 0);
		}

		// If CP0 SR value didn't allow a HW IRQ exception here, it is likely
		//  because a game is currently inside an exception handler.
		//  It is therefore important that the RFE 'return-from-exception'
		//  instruction resets psxRegs.io_cycle_counter to 0. This ensures that
		//  psxBranchTest() is called again as soon as possible so that any
		//  pending HW IRQs are handled.
	}
}

void psxJumpTest() {
	if (!Config.HLE && Config.PsxOut) {
		u32 call = psxRegs.GPR.n.t1 & 0xff;
		switch (psxRegs.pc & 0x1fffff) {
			case 0xa0:
#ifdef PSXBIOS_LOG
				if (call != 0x28 && call != 0xe) {
					PSXBIOS_LOG("Bios call a0: %s (%x) %x,%x,%x,%x\n", biosA0n[call], call, psxRegs.GPR.n.a0, psxRegs.GPR.n.a1, psxRegs.GPR.n.a2, psxRegs.GPR.n.a3); }
#endif
				if (biosA0[call])
					biosA0[call]();
				break;
			case 0xb0:
#ifdef PSXBIOS_LOG
				if (call != 0x17 && call != 0xb) {
					PSXBIOS_LOG("Bios call b0: %s (%x) %x,%x,%x,%x\n", biosB0n[call], call, psxRegs.GPR.n.a0, psxRegs.GPR.n.a1, psxRegs.GPR.n.a2, psxRegs.GPR.n.a3); }
#endif
				if (biosB0[call])
					biosB0[call]();
				break;
			case 0xc0:
#ifdef PSXBIOS_LOG
				PSXBIOS_LOG("Bios call c0: %s (%x) %x,%x,%x,%x\n", biosC0n[call], call, psxRegs.GPR.n.a0, psxRegs.GPR.n.a1, psxRegs.GPR.n.a2, psxRegs.GPR.n.a3);
#endif
				if (biosC0[call])
					biosC0[call]();
				break;
		}
	}
}

void psxExecuteBios() {
	while (psxRegs.pc != 0x80030000)
		psxCpu->ExecuteBlock();
}

