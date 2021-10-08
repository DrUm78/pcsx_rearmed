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
 * PSX assembly interpreter.
 */

#include "psxcommon.h"
#include "r3000a.h"
#include "gte.h"
#include "psxhle.h"
#include "debug.h"

boolean m_inDelaySlot = 0;
struct delay m_delayedLoadInfo[2];
boolean m_inISR = FALSE;
boolean m_nextIsDelaySlot = FALSE;
unsigned m_currentDelayedLoad = 0;

// These macros are used to assemble the repassembler functions

#ifdef PSXCPU_LOG
#define debugI() PSXCPU_LOG("%s\n", disR3000AF(psxRegs.code, psxRegs.pc)); 
#else
#define debugI()
#endif

boolean execI();

// Subsets
void (*psxBSC[64])();
void (*psxSPC[64])();
void (*psxREG[32])();
void (*psxCP0[32])();
void (*psxCP2[64])(struct psxCP2Regs *regs);
void (*psxCP2BSC[32])();

void maybeCancelDelayedLoad(uint32_t index)
{
	unsigned other = m_currentDelayedLoad ^ 1;
	if (m_delayedLoadInfo[other].index == index) m_delayedLoadInfo[other].active = FALSE;
}


#ifdef ICACHE_EMULATION
/*
Formula One 2001 :
Use old CPU cache code when the RAM location is updated with new code (affects in-game racing)
*/
static u8* ICache_Addr;
static u8* ICache_Code;
uint32_t *Read_ICache(uint32_t pc)
{
	uint32_t pc_bank, pc_offset, pc_cache;
	uint8_t *IAddr, *ICode;

	pc_bank = pc >> 24;
	pc_offset = pc & 0xffffff;
	pc_cache = pc & 0xfff;

	IAddr = ICache_Addr;
	ICode = ICache_Code;

	// cached - RAM
	if (pc_bank == 0x80 || pc_bank == 0x00)
	{
		if (SWAP32(*(uint32_t *)(IAddr + pc_cache)) == pc_offset)
		{
			// Cache hit - return last opcode used
			return (uint32_t *)(ICode + pc_cache);
		}
		else
		{
			// Cache miss - addresses don't match
			// - default: 0xffffffff (not init)

			// cache line is 4 bytes wide
			pc_offset &= ~0xf;
			pc_cache &= ~0xf;

			// address line
			*(uint32_t *)(IAddr + pc_cache + 0x0) = SWAP32(pc_offset + 0x0);
			*(uint32_t *)(IAddr + pc_cache + 0x4) = SWAP32(pc_offset + 0x4);
			*(uint32_t *)(IAddr + pc_cache + 0x8) = SWAP32(pc_offset + 0x8);
			*(uint32_t *)(IAddr + pc_cache + 0xc) = SWAP32(pc_offset + 0xc);

			// opcode line
			pc_offset = pc & ~0xf;
			*(uint32_t *)(ICode + pc_cache + 0x0) = psxMu32ref(pc_offset + 0x0);
			*(uint32_t *)(ICode + pc_cache + 0x4) = psxMu32ref(pc_offset + 0x4);
			*(uint32_t *)(ICode + pc_cache + 0x8) = psxMu32ref(pc_offset + 0x8);
			*(uint32_t *)(ICode + pc_cache + 0xc) = psxMu32ref(pc_offset + 0xc);
		}
	}

	/*
	TODO: Probably should add cached BIOS
	*/
	// default
	return (uint32_t *)PSXM(pc);
}
#endif

// this defines shall be used with the tmp 
// of the next func (instead of _Funct_...)
#define _tFunct_  ((tmp      ) & 0x3F)  // The funct part of the instruction register 
#define _tRd_     ((tmp >> 11) & 0x1F)  // The rd part of the instruction register 
#define _tRt_     ((tmp >> 16) & 0x1F)  // The rt part of the instruction register 
#define _tRs_     ((tmp >> 21) & 0x1F)  // The rs part of the instruction register 
#define _tSa_     ((tmp >>  6) & 0x1F)  // The sa part of the instruction register

static uint32_t* delayedLoadRef(unsigned reg, uint32_t mask) {
    if (reg >= 32) abort();
    m_delayedLoadInfo[m_currentDelayedLoad].active = TRUE;
    m_delayedLoadInfo[m_currentDelayedLoad].index = reg;
    m_delayedLoadInfo[m_currentDelayedLoad].mask = mask;
    return &m_delayedLoadInfo[m_currentDelayedLoad].value;
}


#define DELAY_LOAD(r, m) \
    m_delayedLoadInfo[m_currentDelayedLoad].active = TRUE; \
    m_delayedLoadInfo[m_currentDelayedLoad].index = r; \
    m_delayedLoadInfo[m_currentDelayedLoad].mask = m; \

void delayedPCLoad(uint32_t value, boolean fromLink)
{
	//auto &delayedLoad = m_delayedLoadInfo[m_currentDelayedLoad];
	m_delayedLoadInfo[m_currentDelayedLoad].pcActive = TRUE;
	m_delayedLoadInfo[m_currentDelayedLoad].pcValue = value;
	m_delayedLoadInfo[m_currentDelayedLoad].fromLink = fromLink;
}

static void doBranch(uint32_t target, boolean fromLink)
{
    m_nextIsDelaySlot = TRUE;
    delayedPCLoad(target, fromLink);
}

/*********************************************************
* Arithmetic with immediate operand                      *
* Format:  OP rt, rs, immediate                          *
*********************************************************/
void psxADDI() 	{ if (!_Rt_) return; maybeCancelDelayedLoad(_Rt_); _rRt_ = _u32(_rRs_) + _Imm_ ; }		// Rt = Rs + Im 	(Exception on Integer Overflow)
void psxADDIU() { if (!_Rt_) return; maybeCancelDelayedLoad(_Rt_); _rRt_ = _u32(_rRs_) + _Imm_ ; }		// Rt = Rs + Im
void psxANDI() 	{ if (!_Rt_) return; maybeCancelDelayedLoad(_Rt_); _rRt_ = _u32(_rRs_) & _ImmU_; }		// Rt = Rs And Im
void psxORI() 	{ if (!_Rt_) return; maybeCancelDelayedLoad(_Rt_); _rRt_ = _u32(_rRs_) | _ImmU_; }		// Rt = Rs Or  Im
void psxXORI() 	{ if (!_Rt_) return; maybeCancelDelayedLoad(_Rt_); _rRt_ = _u32(_rRs_) ^ _ImmU_; }		// Rt = Rs Xor Im
void psxSLTI() 	{ if (!_Rt_) return; maybeCancelDelayedLoad(_Rt_); _rRt_ = _i32(_rRs_) < _Imm_ ; }		// Rt = Rs < Im		(Signed)
void psxSLTIU() { if (!_Rt_) return; maybeCancelDelayedLoad(_Rt_); _rRt_ = _u32(_rRs_) < ((u32)_Imm_); }		// Rt = Rs < Im		(Unsigned)

/*********************************************************
* Register arithmetic                                    *
* Format:  OP rd, rs, rt                                 *
*********************************************************/
void psxADD()	{ if (!_Rd_) return; maybeCancelDelayedLoad(_Rd_); _rRd_ = _u32(_rRs_) + _u32(_rRt_); }	// Rd = Rs + Rt		(Exception on Integer Overflow)
void psxADDU() 	{ if (!_Rd_) return; maybeCancelDelayedLoad(_Rd_); _rRd_ = _u32(_rRs_) + _u32(_rRt_); }	// Rd = Rs + Rt
void psxSUB() 	{ if (!_Rd_) return; maybeCancelDelayedLoad(_Rd_); _rRd_ = _u32(_rRs_) - _u32(_rRt_); }	// Rd = Rs - Rt		(Exception on Integer Overflow)
void psxSUBU() 	{ if (!_Rd_) return; maybeCancelDelayedLoad(_Rd_); _rRd_ = _u32(_rRs_) - _u32(_rRt_); }	// Rd = Rs - Rt
void psxAND() 	{ if (!_Rd_) return; maybeCancelDelayedLoad(_Rd_); _rRd_ = _u32(_rRs_) & _u32(_rRt_); }	// Rd = Rs And Rt
void psxOR() 	{ if (!_Rd_) return; maybeCancelDelayedLoad(_Rd_); _rRd_ = _u32(_rRs_) | _u32(_rRt_); }	// Rd = Rs Or  Rt
void psxXOR() 	{ if (!_Rd_) return; maybeCancelDelayedLoad(_Rd_); _rRd_ = _u32(_rRs_) ^ _u32(_rRt_); }	// Rd = Rs Xor Rt
void psxNOR() 	{ if (!_Rd_) return; maybeCancelDelayedLoad(_Rd_); _rRd_ =~(_u32(_rRs_) | _u32(_rRt_)); }// Rd = Rs Nor Rt
void psxSLT() 	{ if (!_Rd_) return; maybeCancelDelayedLoad(_Rd_); _rRd_ = _i32(_rRs_) < _i32(_rRt_); }	// Rd = Rs < Rt		(Signed)
void psxSLTU() 	{ if (!_Rd_) return; maybeCancelDelayedLoad(_Rd_); _rRd_ = _u32(_rRs_) < _u32(_rRt_); }	// Rd = Rs < Rt		(Unsigned)

/*********************************************************
* Register mult/div & Register trap logic                *
* Format:  OP rs, rt                                     *
*********************************************************/
void psxDIV() {
    if (!_i32(_rRt_)) {
        _i32(_rHi_) = _i32(_rRs_);
        if (_i32(_rRs_) & 0x80000000) {
            _i32(_rLo_) = 1;
        } else {
            _i32(_rLo_) = 0xFFFFFFFF;
        }
/*
 * Notaz said that this was "not needed" for ARM platforms and could slow it down so let's disable for ARM. 
 * This fixes a crash issue that can happen when running Amidog's CPU test.
 * (It still stays stuck to a black screen but at least it doesn't crash anymore)
 */
#if !defined(__arm__) && !defined(__aarch64__)
    } else if (_i32(_rRs_) == 0x80000000 && _i32(_rRt_) == 0xFFFFFFFF) {
        _i32(_rLo_) = 0x80000000;
        _i32(_rHi_) = 0;
#endif
    } else {
        _i32(_rLo_) = _i32(_rRs_) / _i32(_rRt_);
        _i32(_rHi_) = _i32(_rRs_) % _i32(_rRt_);
    }
}

void psxDIVU() {
	if (_rRt_ != 0) {
		_rLo_ = _rRs_ / _rRt_;
		_rHi_ = _rRs_ % _rRt_;
	}
	else {
		_i32(_rLo_) = 0xffffffff;
		_i32(_rHi_) = _i32(_rRs_);
	}
}

void psxMULT() {
	u64 res = (s64)((s64)_i32(_rRs_) * (s64)_i32(_rRt_));

	psxRegs.GPR.n.lo = (u32)(res & 0xffffffff);
	psxRegs.GPR.n.hi = (u32)((res >> 32) & 0xffffffff);
}

void psxMULTU() {
	u64 res = (u64)((u64)_u32(_rRs_) * (u64)_u32(_rRt_));

	psxRegs.GPR.n.lo = (u32)(res & 0xffffffff);
	psxRegs.GPR.n.hi = (u32)((res >> 32) & 0xffffffff);
}

/*********************************************************
* Register m_inDelaySlot logic                                  *
* Format:  OP rs, offset                                 *
*********************************************************/
#define RepZBranchi32(op)      if(_i32(_rRs_) op 0) doBranch(_BranchTarget_, FALSE);

#define RepZBranchLinki32(op)                                    \
    {                                                            \
        uint32_t ra = psxRegs.pc + 4;                          \
        psxRegs.GPR.r[31] = ra;                                \
        maybeCancelDelayedLoad(31);                              \
        if (_i32(_rRs_) op 0) {                                  \
            doBranch(_BranchTarget_, TRUE);                      \
        }                                                        \
    }

void psxBGEZ()   { RepZBranchi32(>=) }      // Branch if Rs >= 0
void psxBGEZAL() { RepZBranchLinki32(>=) }  // Branch if Rs >= 0 and link
void psxBGTZ()   { RepZBranchi32(>) }       // Branch if Rs >  0
void psxBLEZ()   { RepZBranchi32(<=) }      // Branch if Rs <= 0
void psxBLTZ()   { RepZBranchi32(<) }       // Branch if Rs <  0
void psxBLTZAL() { RepZBranchLinki32(<) }   // Branch if Rs <  0 and link

/*********************************************************
* Shift arithmetic with constant shift                   *
* Format:  OP rd, rt, sa                                 *
*********************************************************/
void psxSLL() { if (!_Rd_) return; _u32(_rRd_) = _u32(_rRt_) << _Sa_; } // Rd = Rt << sa
void psxSRA() { if (!_Rd_) return; _i32(_rRd_) = _i32(_rRt_) >> _Sa_; } // Rd = Rt >> sa (arithmetic)
void psxSRL() { if (!_Rd_) return; _u32(_rRd_) = _u32(_rRt_) >> _Sa_; } // Rd = Rt >> sa (logical)

/*********************************************************
* Shift arithmetic with variant register shift           *
* Format:  OP rd, rt, rs                                 *
*********************************************************/
void psxSLLV() { if (!_Rd_) return; _u32(_rRd_) = _u32(_rRt_) << (_u32(_rRs_) & 0x1F); } // Rd = Rt << rs
void psxSRAV() { if (!_Rd_) return; _i32(_rRd_) = _i32(_rRt_) >> (_u32(_rRs_) & 0x1F); } // Rd = Rt >> rs (arithmetic)
void psxSRLV() { if (!_Rd_) return; _u32(_rRd_) = _u32(_rRt_) >> (_u32(_rRs_) & 0x1F); } // Rd = Rt >> rs (logical)

/*********************************************************
* Load higher 16 bits of the first word in GPR with imm  *
* Format:  OP rt, immediate                              *
*********************************************************/
void psxLUI() { if (!_Rt_) return; _u32(_rRt_) = psxRegs.code << 16; } // Upper halfword of Rt = Im

/*********************************************************
* Move from HI/LO to GPR                                 *
* Format:  OP rd                                         *
*********************************************************/
void psxMFHI() { if (!_Rd_) return; _rRd_ = _rHi_; } // Rd = Hi
void psxMFLO() { if (!_Rd_) return; _rRd_ = _rLo_; } // Rd = Lo

/*********************************************************
* Move to GPR to HI/LO & Register jump                   *
* Format:  OP rs                                         *
*********************************************************/
void psxMTHI() { _rHi_ = _rRs_; } // Hi = Rs
void psxMTLO() { _rLo_ = _rRs_; } // Lo = Rs

/*********************************************************
* Special purpose instructions                           *
* Format:  OP                                            *
*********************************************************/
void psxBREAK() {
	psxRegs.pc -= 4;
	psxException(0x24, m_inDelaySlot);
    if (m_inDelaySlot) {
        if (!m_delayedLoadInfo[m_currentDelayedLoad].pcActive) abort();
        m_delayedLoadInfo[m_currentDelayedLoad].pcActive = FALSE;
    }
}

void psxSYSCALL() {
	psxRegs.pc -= 4;
	psxException(0x20, m_inDelaySlot);
    if (m_inDelaySlot) {
        if (!m_delayedLoadInfo[m_currentDelayedLoad].pcActive) abort();
        m_delayedLoadInfo[m_currentDelayedLoad].pcActive = FALSE;
    }
}

void psxRFE() {
//	SysPrintf("psxRFE\n");
	psxRegs.CP0.n.Status = (psxRegs.CP0.n.Status & 0xfffffff0) |
						  ((psxRegs.CP0.n.Status & 0x3c) >> 2);
	psxTestSWInts();
}

/*********************************************************
* Register m_inDelaySlot logic                                  *
* Format:  OP rs, rt, offset                             *
*********************************************************/
#define RepBranchi32(op)      if(_i32(_rRs_) op _i32(_rRt_)) doBranch(_BranchTarget_, FALSE);

void psxBEQ() {	RepBranchi32(==) }  // Branch if Rs == Rt
void psxBNE() {	RepBranchi32(!=) }  // Branch if Rs != Rt

/*********************************************************
* Jump to target                                         *
* Format:  OP target                                     *
*********************************************************/
void psxJ()   {               doBranch(_JumpTarget_, FALSE); }
void psxJAL()
{
    maybeCancelDelayedLoad(31);
    uint32_t ra = psxRegs.pc + 4;
    psxRegs.GPR.r[31] = ra;
	doBranch(_JumpTarget_, TRUE);
}

/*********************************************************
* Register jump                                          *
* Format:  OP rs, rd                                     *
*********************************************************/
void psxJR()   {
	doBranch(_rRs_ & ~3, FALSE);
	psxJumpTest();
}

void psxJALR() {
	u32 temp = _u32(_rRs_);
	if (_Rd_)
	{
		maybeCancelDelayedLoad(_Rd_);
		uint32_t ra = psxRegs.pc + 4;
		_rRd_ = ra;
	}
	doBranch(temp & ~3, TRUE);
}

/*********************************************************
* Load and store for GPR                                 *
* Format:  OP rt, offset(base)                           *
*********************************************************/

#define _oB_ (_u32(_rRs_) + _Imm_)

void psxLB() {
	if (_Rt_) {
		DELAY_LOAD(_Rt_, 0);
		_i32(m_delayedLoadInfo[m_currentDelayedLoad].value) = (signed char)psxMemRead8(_oB_);	
	} else {
		psxMemRead8(_oB_); 
	}
}

void psxLBU() {
	if (_Rt_) {
		DELAY_LOAD(_Rt_, 0);
		_u32(m_delayedLoadInfo[m_currentDelayedLoad].value) = psxMemRead8(_oB_);	
	} else {
		psxMemRead8(_oB_); 
	}
}

void psxLH() {
	if (_Rt_) {
		DELAY_LOAD(_Rt_, 0);
		_i32(m_delayedLoadInfo[m_currentDelayedLoad].value) = (short)psxMemRead16(_oB_);
	} else {
		psxMemRead16(_oB_);
	}
}

void psxLHU() {
	if (_Rt_) {
		DELAY_LOAD(_Rt_, 0);
		_u32(m_delayedLoadInfo[m_currentDelayedLoad].value) = psxMemRead16(_oB_);
	} else {
		psxMemRead16(_oB_);
	}
}

void psxLW() {
	if (_Rt_) {
		DELAY_LOAD(_Rt_, 0);
		_u32(m_delayedLoadInfo[m_currentDelayedLoad].value) = psxMemRead32(_oB_);
	} else {
		psxMemRead32(_oB_);
	}
}

u32 LWL_MASK[4] = { 0xffffff, 0xffff, 0xff, 0 };
u32 LWL_SHIFT[4] = { 24, 16, 8, 0 };

void psxLWL() {
	u32 addr = _oB_;
	u32 shift = addr & 3;
	u32 mem = psxMemRead32(addr & ~3);

	if (!_Rt_) return;
	
	DELAY_LOAD(_Rt_, LWL_MASK[shift]);
	_u32(m_delayedLoadInfo[m_currentDelayedLoad].value) = mem >> LWL_SHIFT[shift];	

	/*
	Mem = 1234.  Reg = abcd

	0   4bcd   (mem << 24) | (reg & 0x00ffffff)
	1   34cd   (mem << 16) | (reg & 0x0000ffff)
	2   234d   (mem <<  8) | (reg & 0x000000ff)
	3   1234   (mem      ) | (reg & 0x00000000)
	*/
}

u32 LWR_MASK[4] = { 0, 0xff000000, 0xffff0000, 0xffffff00 };
u32 LWR_SHIFT[4] = { 0, 8, 16, 24 };

void psxLWR() {
	u32 addr = _oB_;
	u32 shift = addr & 3;
	u32 mem = psxMemRead32(addr & ~3);

	if (!_Rt_) return;
	
	DELAY_LOAD(_Rt_, LWR_MASK[shift]);
	// _u32(delayedLoadRef(_Rt_, LWR_MASK[shift])) = mem >> LWR_SHIFT[shift];
	_u32(m_delayedLoadInfo[m_currentDelayedLoad].value) = mem >> LWR_SHIFT[shift];	

	/*
	Mem = 1234.  Reg = abcd

	0   1234   (mem      ) | (reg & 0x00000000)
	1   a123   (mem >>  8) | (reg & 0xff000000)
	2   ab12   (mem >> 16) | (reg & 0xffff0000)
	3   abc1   (mem >> 24) | (reg & 0xffffff00)
	*/
}

void psxSB() { psxMemWrite8 (_oB_, _rRt_ &   0xff); }
void psxSH() { psxMemWrite16(_oB_, _rRt_ & 0xffff); }
void psxSW() { psxMemWrite32(_oB_, _rRt_); }

u32 SWL_MASK[4] = { 0xffffff00, 0xffff0000, 0xff000000, 0 };
u32 SWL_SHIFT[4] = { 24, 16, 8, 0 };

void psxSWL() {
	u32 addr = _oB_;
	u32 shift = addr & 3;
	u32 mem = psxMemRead32(addr & ~3);

	psxMemWrite32(addr & ~3,  (_u32(_rRt_) >> SWL_SHIFT[shift]) |
			     (  mem & SWL_MASK[shift]) );
	/*
	Mem = 1234.  Reg = abcd

	0   123a   (reg >> 24) | (mem & 0xffffff00)
	1   12ab   (reg >> 16) | (mem & 0xffff0000)
	2   1abc   (reg >>  8) | (mem & 0xff000000)
	3   abcd   (reg      ) | (mem & 0x00000000)
	*/
}

u32 SWR_MASK[4] = { 0, 0xff, 0xffff, 0xffffff };
u32 SWR_SHIFT[4] = { 0, 8, 16, 24 };

void psxSWR() {
	u32 addr = _oB_;
	u32 shift = addr & 3;
	u32 mem = psxMemRead32(addr & ~3);

	psxMemWrite32(addr & ~3,  (_u32(_rRt_) << SWR_SHIFT[shift]) |
			     (  mem & SWR_MASK[shift]) );

	/*
	Mem = 1234.  Reg = abcd

	0   abcd   (reg      ) | (mem & 0x00000000)
	1   bcd4   (reg <<  8) | (mem & 0x000000ff)
	2   cd34   (reg << 16) | (mem & 0x0000ffff)
	3   d234   (reg << 24) | (mem & 0x00ffffff)
	*/
}

/*********************************************************
* Moves between GPR and COPx                             *
* Format:  OP rt, fs                                     *
*********************************************************/
void psxMFC0()
{
    // load delay = 1 latency
    if (!_Rt_) return;

	DELAY_LOAD(_Rt_, 0);
	_i32(m_delayedLoadInfo[m_currentDelayedLoad].value) = (int)_rFs_;
}

void psxCFC0()
{
    // load delay = 1 latency
    if (!_Rt_) return;
    
	DELAY_LOAD(_Rt_, 0);
	_i32(m_delayedLoadInfo[m_currentDelayedLoad].value) = (int)_rFs_;
}

void psxTestSWInts() {
	if (psxRegs.CP0.n.Cause & psxRegs.CP0.n.Status & 0x0300 &&
	   psxRegs.CP0.n.Status & 0x1) {
		//psxRegs.CP0.n.Cause &= ~0x7c;
        boolean inDelaySlot = m_inDelaySlot;
        m_inDelaySlot = FALSE;
		psxException(psxRegs.CP0.n.Cause, inDelaySlot);
	}
}

void MTC0(int reg, u32 val) {
//	SysPrintf("MTC0 %d: %x\n", reg, val);
	switch (reg) {
		case 12: // Status
			psxRegs.CP0.n.Status = val;
			psxTestSWInts();
			break;

		case 13: // Cause
			psxRegs.CP0.n.Cause = val & ~(0xfc00);
			psxTestSWInts();
			break;

		default:
			psxRegs.CP0.r[reg] = val;
			break;
	}
}

void psxMTC0() { MTC0(_Rd_, _u32(_rRt_)); }
void psxCTC0() { MTC0(_Rd_, _u32(_rRt_)); }

/*
void psxMFC2(uint32_t code) {
    // load delay = 1 latency
    
	//_i32(_Rt_) = psxCP2BSC[0]();
	//delayedLoadRef(_Rt_, 0) = psxCP2BSC[0]();
}

void psxCFC2(uint32_t code) {
    // load delay = 1 latency
	//_i32(_Rt_) = psxCP2BSC[2]();
}
*/

/*********************************************************
* Unknow instruction (would generate an exception)       *
* Format:  ?                                             *
*********************************************************/
void psxNULL() { 
#ifdef PSXCPU_LOG
	PSXCPU_LOG("psx: Unimplemented op %x\n", psxRegs.code);
#endif
}

void psxSPECIAL() {
	psxSPC[_Funct_]();
}

void psxREGIMM() {
	psxREG[_Rt_]();
}

void psxCOP0() {
	psxCP0[_Rs_]();
}

void psxCOP2() {
	psxCP2[_Funct_]((struct psxCP2Regs *)&psxRegs.CP2D);
}

void psxBASIC(struct psxCP2Regs *regs) {
	psxCP2BSC[_Rs_]();
}

void psxHLE() {
//	psxHLEt[psxRegs.code & 0xffff]();
//	psxHLEt[psxRegs.code & 0x07]();		// HDHOSHY experimental patch
    uint32_t hleCode = psxRegs.code & 0x03ffffff;
    if (hleCode >= (sizeof(psxHLEt) / sizeof(psxHLEt[0]))) {
        psxNULL();
    } else {
        psxHLEt[hleCode]();
    }
}

void (*psxBSC[64])() = {
	psxSPECIAL, psxREGIMM, psxJ   , psxJAL  , psxBEQ , psxBNE , psxBLEZ, psxBGTZ,
	psxADDI   , psxADDIU , psxSLTI, psxSLTIU, psxANDI, psxORI , psxXORI, psxLUI ,
	psxCOP0   , psxNULL  , psxCOP2, psxNULL , psxNULL, psxNULL, psxNULL, psxNULL,
	psxNULL   , psxNULL  , psxNULL, psxNULL , psxNULL, psxNULL, psxNULL, psxNULL,
	psxLB     , psxLH    , psxLWL , psxLW   , psxLBU , psxLHU , psxLWR , psxNULL,
	psxSB     , psxSH    , psxSWL , psxSW   , psxNULL, psxNULL, psxSWR , psxNULL, 
	psxNULL   , psxNULL  , gteLWC2, psxNULL , psxNULL, psxNULL, psxNULL, psxNULL,
	psxNULL   , psxNULL  , gteSWC2, psxHLE  , psxNULL, psxNULL, psxNULL, psxNULL 
};


void (*psxSPC[64])() = {
	psxSLL , psxNULL , psxSRL , psxSRA , psxSLLV   , psxNULL , psxSRLV, psxSRAV,
	psxJR  , psxJALR , psxNULL, psxNULL, psxSYSCALL, psxBREAK, psxNULL, psxNULL,
	psxMFHI, psxMTHI , psxMFLO, psxMTLO, psxNULL   , psxNULL , psxNULL, psxNULL,
	psxMULT, psxMULTU, psxDIV , psxDIVU, psxNULL   , psxNULL , psxNULL, psxNULL,
	psxADD , psxADDU , psxSUB , psxSUBU, psxAND    , psxOR   , psxXOR , psxNOR ,
	psxNULL, psxNULL , psxSLT , psxSLTU, psxNULL   , psxNULL , psxNULL, psxNULL,
	psxNULL, psxNULL , psxNULL, psxNULL, psxNULL   , psxNULL , psxNULL, psxNULL,
	psxNULL, psxNULL , psxNULL, psxNULL, psxNULL   , psxNULL , psxNULL, psxNULL
};

void (*psxREG[32])() = {
	psxBLTZ  , psxBGEZ  , psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
	psxNULL  , psxNULL  , psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
	psxBLTZAL, psxBGEZAL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
	psxNULL  , psxNULL  , psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL
};

void (*psxCP0[32])() = {
	psxMFC0, psxNULL, psxCFC0, psxNULL, psxMTC0, psxNULL, psxCTC0, psxNULL,
	psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
	psxRFE , psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
	psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL
};

void (*psxCP2[64])(struct psxCP2Regs *regs) = {
	psxBASIC, gteRTPS , psxNULL , psxNULL, psxNULL, psxNULL , gteNCLIP, psxNULL, // 00
	psxNULL , psxNULL , psxNULL , psxNULL, gteOP  , psxNULL , psxNULL , psxNULL, // 08
	gteDPCS , gteINTPL, gteMVMVA, gteNCDS, gteCDP , psxNULL , gteNCDT , psxNULL, // 10
	psxNULL , psxNULL , psxNULL , gteNCCS, gteCC  , psxNULL , gteNCS  , psxNULL, // 18
	gteNCT  , psxNULL , psxNULL , psxNULL, psxNULL, psxNULL , psxNULL , psxNULL, // 20
	gteSQR  , gteDCPL , gteDPCT , psxNULL, psxNULL, gteAVSZ3, gteAVSZ4, psxNULL, // 28 
	gteRTPT , psxNULL , psxNULL , psxNULL, psxNULL, psxNULL , psxNULL , psxNULL, // 30
	psxNULL , psxNULL , psxNULL , psxNULL, psxNULL, gteGPF  , gteGPL  , gteNCCT  // 38
};

void (*psxCP2BSC[32])() = {
	gteMFC2, psxNULL, gteCFC2, psxNULL, gteMTC2, psxNULL, gteCTC2, psxNULL,
	psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
	psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
	psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL
};


///////////////////////////////////////////

static int intInit() {
	#ifdef ICACHE_EMULATION
	/* We have to allocate the icache memory even if 
	 * the user has not enabled it as otherwise it can cause issues.
	 */
	if (!ICache_Addr)
	{
		ICache_Addr = malloc(0x1000);
		if (!ICache_Addr)
		{
			return -1;
		}
	}

	if (!ICache_Code)
	{
		ICache_Code = malloc(0x1000);
		if (!ICache_Code)
		{
			return -1;
		}
	}
	memset(ICache_Addr, 0xff, 0x1000);
	memset(ICache_Code, 0xff, 0x1000);
	#endif
	return 0;
}

static void intReset() {
	#ifdef ICACHE_EMULATION
	memset(ICache_Addr, 0xff, 0x1000);
	memset(ICache_Code, 0xff, 0x1000);
	#endif
    m_nextIsDelaySlot = FALSE;
    m_inDelaySlot = FALSE;
    m_delayedLoadInfo[0].active = FALSE;
    m_delayedLoadInfo[1].active = FALSE;
    m_delayedLoadInfo[0].pcActive = FALSE;
    m_delayedLoadInfo[1].pcActive = FALSE;
}

void intExecute() {
	extern int stop;
	for (;!stop;)
	{
		while (!execI());
	}
}

void intExecuteBlock() {
    while (!execI());
}

static void intClear(u32 Addr, u32 Size) {
}

void intNotify (int note, void *data) {
	#ifdef ICACHE_EMULATION
	/* Gameblabla - Only clear the icache if it's isolated */
	if (note == R3000ACPU_NOTIFY_CACHE_ISOLATED)
	{
		memset(ICache_Addr, 0xff, 0x1000);
		memset(ICache_Code, 0xff, 0x1000);
	}
	#endif
}

static void intShutdown() {
	#ifdef ICACHE_EMULATION
	if (ICache_Addr)
	{
		free(ICache_Addr);
		ICache_Addr = NULL;
	}

	if (ICache_Code)
	{
		free(ICache_Code);
		ICache_Code = NULL;
	}
	#endif
}

// interpreter execution
boolean execI() {
	u32 *code;
	
	boolean ranDelaySlot = FALSE;

	if (m_nextIsDelaySlot)
	{
		m_inDelaySlot = TRUE;
		m_nextIsDelaySlot = FALSE;
	}
		
	#ifdef ICACHE_EMULATION
	if (Config.icache_emulation) code = Read_ICache(psxRegs.pc);
	else
	#endif
	{
		code = (u32 *)PSXM(psxRegs.pc);
	}
	psxRegs.code = ((code == NULL) ? 0 : SWAP32(*code));

	debugI();

	if (Config.Debug) ProcessDebug();
	psxRegs.pc += 4;
	psxRegs.cycle += BIAS;

	psxBSC[psxRegs.code >> 26]();
		
	m_currentDelayedLoad ^= 1;
	//printf("m_currentDelayedLoad %d\n", m_currentDelayedLoad);
	if (m_delayedLoadInfo[m_currentDelayedLoad].active)
	{
		uint32_t reg = psxRegs.GPR.r[m_delayedLoadInfo[m_currentDelayedLoad].index];
		reg &= m_delayedLoadInfo[m_currentDelayedLoad].mask;
		reg |= m_delayedLoadInfo[m_currentDelayedLoad].value;
		psxRegs.GPR.r[m_delayedLoadInfo[m_currentDelayedLoad].index] = reg;
		m_delayedLoadInfo[m_currentDelayedLoad].active = FALSE;
	}
	if (m_delayedLoadInfo[m_currentDelayedLoad].pcActive) {
		psxRegs.pc = m_delayedLoadInfo[m_currentDelayedLoad].pcValue;
		m_delayedLoadInfo[m_currentDelayedLoad].pcActive = FALSE;
		m_delayedLoadInfo[m_currentDelayedLoad].fromLink = FALSE;
	}
	if (m_inDelaySlot) {
		m_inDelaySlot = FALSE;
		ranDelaySlot = TRUE;
		psxBranchTest();
	}
	
	return ranDelaySlot;
}

R3000Acpu psxInt = {
	intInit,
	intReset,
	intExecute,
	intExecuteBlock,
	intClear,
#ifdef ICACHE_EMULATION
	intNotify,
#endif
	intShutdown
};
