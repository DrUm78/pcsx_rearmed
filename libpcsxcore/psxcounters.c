/***************************************************************************
 *   Copyright (C) 2010 by Blade_Arma                                      *
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
 * Internal PSX counters.
 */

///////////////////////////////////////////////////////////////////////////////
//senquack - NOTE: Root counters code here has been updated to match Notaz's
// PCSX Rearmed where possible. Important changes include:
// * Proper handling of counter overflows.
// * VBlank root counter (counter 3) is triggered only as often as needed,
//   not every HSync.
// * SPU updates occur using new event queue (psxevents.cpp)
// * Some optimizations, more accurate calculation of timer updates.
//
// TODO : Implement direct rootcounter mem access of Rearmed dynarec?
//        (see https://github.com/notaz/pcsx_rearmed/commit/b1be1eeee94d3547c20719acfa6b0082404897f1 )
//        Seems to make Parasite Eve 2 RCntFix hard to implement, though.
// TODO : Implement Rearmed's auto-frameskip so SPU doesn't need to
//        hackishly be updated twice per emulated frame.
// TODO : Implement Rearmed's frame limiter

#include "psxcounters.h"
#include "psxevents.h"
#include "gpu.h"
#include "cheat.h"

/******************************************************************************/

enum
{
    Rc0Gate           = 0x0001, // 0    not implemented
    Rc1Gate           = 0x0001, // 0    not implemented
    Rc2Disable        = 0x0001, // 0    partially implemented
    RcUnknown1        = 0x0002, // 1    ?
    RcUnknown2        = 0x0004, // 2    ?
    RcCountToTarget   = 0x0008, // 3
    RcIrqOnTarget     = 0x0010, // 4
    RcIrqOnOverflow   = 0x0020, // 5
    RcIrqRegenerate   = 0x0040, // 6
    RcUnknown7        = 0x0080, // 7    ?
    Rc0PixelClock     = 0x0100, // 8    fake implementation
    Rc1HSyncClock     = 0x0100, // 8
    Rc2Unknown8       = 0x0100, // 8    ?
    Rc0Unknown9       = 0x0200, // 9    ?
    Rc1Unknown9       = 0x0200, // 9    ?
    Rc2OneEighthClock = 0x0200, // 9
    RcUnknown10       = 0x0400, // 10   ?
    RcCountEqTarget   = 0x0800, // 11
    RcOverflow        = 0x1000, // 12
    RcUnknown13       = 0x2000, // 13   ? (always zero)
    RcUnknown14       = 0x4000, // 14   ? (always zero)
    RcUnknown15       = 0x8000, // 15   ? (always zero)
};

#define CounterQuantity  4
#define CountToOverflow  0
#define CountToTarget    1

Rcnt rcnts[ CounterQuantity ];

const uint32_t FrameRate[2] = { 60, 50 };

//senquack - Originally {262,312}, updated to match Rearmed:
const uint32_t HSyncTotal[2] = { 263, 314 };

//senquack - TODO: PCSX Reloaded uses {243,256} here, and Rearmed
// does away with array completely and uses 240 in all cases:
//static const uint32_t VBlankStart[]    = { 240, 256 };
static const uint32_t VBlankStart = 240;

/******************************************************************************/

uint32_t hSyncCount = 0;

uint32_t frame_counter = 0;
static uint32_t hsync_steps = 0;
static uint32_t base_cycle = 0;

//senquack - Originally separate variables, now handled together with
// all other scheduled emu events as new event type PSXINT_RCNT
#define psxNextCounter psxRegs.intCycle[PSXINT_RCNT].cycle
#define psxNextsCounter psxRegs.intCycle[PSXINT_RCNT].sCycle

/******************************************************************************/

static inline void setIrq( uint32_t irq )
{
	psxHu32ref(0x1070) |= SWAPu32(irq);
    	ResetIoCycle();
}

//senquack - Added verboseLog & VERBOSE_LEVEL from PCSX Rearmed:
#define VERBOSE_LEVEL 0

static void verboseLog( uint32_t level, const char *str, ... )
{
#if VERBOSE_LEVEL > 0
    if( level <= VerboseLevel )
    {
        va_list va;
        char buf[ 4096 ];

        va_start( va, str );
        vsprintf( buf, str, va );
        va_end( va );

        printf( "%s", buf );
        fflush( stdout );
    }
#endif
}

/******************************************************************************/

static inline void _psxRcntWcount( uint32_t index, uint32_t value )
{
    if( value > 0xffff )
    {
        verboseLog( 1, "[RCNT %i] wcount > 0xffff: %x\n", index, value );
        value &= 0xffff;
    }

    rcnts[index].cycleStart  = psxRegs.cycle;
    rcnts[index].cycleStart -= value * rcnts[index].rate;

    // TODO: <=.
    if( value < rcnts[index].target )
    {
        rcnts[index].cycle = rcnts[index].target * rcnts[index].rate;
        rcnts[index].counterState = CountToTarget;
    }
    else
    {
        rcnts[index].cycle = 0x10000 * rcnts[index].rate;
        rcnts[index].counterState = CountToOverflow;
    }
}

static inline uint32_t _psxRcntRcount( uint32_t index )
{
    uint32_t count;

    count  = psxRegs.cycle;
    count -= rcnts[index].cycleStart;
    if (rcnts[index].rate > 1)
        count /= rcnts[index].rate;

    if( count > 0x10000 )
    {
        verboseLog( 1, "[RCNT %i] rcount > 0x10000: %x\n", index, count );
    }
    count &= 0xffff;

    return count;
}

//senquack - Added from PCSX Rearmed:
static void _psxRcntWmode( uint32_t index, uint32_t value )
{
    rcnts[index].mode = value;

    switch( index )
    {
        case 0:
            if( value & Rc0PixelClock )
            {
                rcnts[index].rate = 5;
            }
            else
            {
                rcnts[index].rate = 1;
            }
        break;
        case 1:
            if( value & Rc1HSyncClock )
            {
                rcnts[index].rate = (PSXCLK / (FrameRate[Config.PsxType] * HSyncTotal[Config.PsxType]));
            }
            else
            {
                rcnts[index].rate = 1;
            }
        break;
        case 2:
            if( value & Rc2OneEighthClock )
            {
                rcnts[index].rate = 8;
            }
            else
            {
                rcnts[index].rate = 1;
            }

            // TODO: wcount must work.
            if( value & Rc2Disable )
            {
                rcnts[index].rate = 0xffffffff;
            }
        break;
    }
}

/******************************************************************************/

static void psxRcntSet(void)
{
    int32_t countToUpdate;
    uint32_t i;

    psxNextsCounter = psxRegs.cycle;
    psxNextCounter  = 0x7fffffff;

    for( i = 0; i < CounterQuantity; ++i )
    {
        countToUpdate = rcnts[i].cycle - (psxNextsCounter - rcnts[i].cycleStart);

        if( countToUpdate < 0 )
        {
            psxNextCounter = 0;
            break;
        }

        if( countToUpdate < (int32_t)psxNextCounter )
        {
            psxNextCounter = countToUpdate;
        }
    }

    // Any previously queued PSXINT_RCNT event will be replaced
    psxEvqueueAdd(PSXINT_RCNT, psxNextCounter);
}

/******************************************************************************/

static void psxRcntReset( uint32_t index )
{
    uint32_t rcycles;

    rcnts[index].mode |= RcUnknown10;

    if( rcnts[index].counterState == CountToTarget )
    {
        rcycles = psxRegs.cycle - rcnts[index].cycleStart;
        if( rcnts[index].mode & RcCountToTarget )
        {
            rcycles -= rcnts[index].target * rcnts[index].rate;
            rcnts[index].cycleStart = psxRegs.cycle - rcycles;
        }
        else
        {
            rcnts[index].cycle = 0x10000 * rcnts[index].rate;
            rcnts[index].counterState = CountToOverflow;
        }

        if( rcnts[index].mode & RcIrqOnTarget )
        {
            if( (rcnts[index].mode & RcIrqRegenerate) || (!rcnts[index].irqState) )
            {
                verboseLog( 3, "[RCNT %i] irq\n", index );
                setIrq( rcnts[index].irq );
                rcnts[index].irqState = 1;
            }
        }

        rcnts[index].mode |= RcCountEqTarget;

        if( rcycles < 0x10000 * rcnts[index].rate )
            return;
    }

    if( rcnts[index].counterState == CountToOverflow )
    {
        rcycles = psxRegs.cycle - rcnts[index].cycleStart;
        rcycles -= 0x10000 * rcnts[index].rate;

        rcnts[index].cycleStart = psxRegs.cycle - rcycles;

        if( rcycles < rcnts[index].target * rcnts[index].rate )
        {
            rcnts[index].cycle = rcnts[index].target * rcnts[index].rate;
            rcnts[index].counterState = CountToTarget;
        }

        if( rcnts[index].mode & RcIrqOnOverflow )
        {
            if( (rcnts[index].mode & RcIrqRegenerate) || (!rcnts[index].irqState) )
            {
                verboseLog( 3, "[RCNT %i] irq\n", index );
                setIrq( rcnts[index].irq );
                rcnts[index].irqState = 1;
            }
        }

        rcnts[index].mode |= RcOverflow;
    }
}

void psxRcntUpdate()
{
    uint32_t cycle;

    cycle = psxRegs.cycle;

    // rcnt 0.
    if( cycle - rcnts[0].cycleStart >= rcnts[0].cycle )
    {
        psxRcntReset( 0 );
    }

    // rcnt 1.
    if( cycle - rcnts[1].cycleStart >= rcnts[1].cycle )
    {
        psxRcntReset( 1 );
    }

    // rcnt 2.
    if( cycle - rcnts[2].cycleStart >= rcnts[2].cycle )
    {
        psxRcntReset( 2 );
    }

    // rcnt base.
    if( cycle - rcnts[3].cycleStart >= rcnts[3].cycle )
    {
        uint32_t leftover_cycles = cycle - rcnts[3].cycleStart - rcnts[3].cycle;
        uint32_t next_vsync;

        hSyncCount += hsync_steps;

        // VSync irq.
        if( hSyncCount == VBlankStart )
        {
            HW_GPU_STATUS &= ~PSXGPU_LCF;

#ifdef USE_GPULIB
            GPU_vBlank( 1, 0 );
#endif
            setIrq( 0x01 );

            // Do framelimit, frameskip, perf stats, controls, etc:
            // NOTE: this is point of control transfer to frontend menu
            EmuUpdate();

            GPU_updateLace();

            if( SPU_async )
            {
                SPU_async( cycle, 1 );
            }
        }

        // Update lace. (with InuYasha fix)
        if( hSyncCount >= (Config.VSyncWA ? HSyncTotal[Config.PsxType]/BIAS : HSyncTotal[Config.PsxType]) )
        {
            hSyncCount = 0;
            frame_counter++;

            gpuSyncPluginSR();
            if( (HW_GPU_STATUS & PSXGPU_ILACE_BITS) == PSXGPU_ILACE_BITS )
                HW_GPU_STATUS |= frame_counter << 31;

#ifdef USE_GPULIB
            GPU_vBlank( 0, HW_GPU_STATUS >> 31 );
#endif
        }

        // Schedule next call, in hsyncs
        hsync_steps = HSyncTotal[Config.PsxType] - hSyncCount;
        next_vsync = VBlankStart - hSyncCount; // ok to overflow
        if( next_vsync && next_vsync < hsync_steps )
            hsync_steps = next_vsync;

        rcnts[3].cycleStart = cycle - leftover_cycles;
        if (Config.PsxType)
                // 20.12 precision, clk / 50 / 313 ~= 2164.14
                base_cycle += hsync_steps * 8864320;
        else
                // clk / 60 / 263 ~= 2146.31
                base_cycle += hsync_steps * 8791293;
        rcnts[3].cycle = base_cycle >> 12;
        base_cycle &= 0xfff;
    }

    psxRcntSet();
}

/******************************************************************************/

void psxRcntWcount( uint32_t index, uint32_t value )
{
    verboseLog( 2, "[RCNT %i] wcount: %x\n", index, value );

    _psxRcntWcount( index, value );
    psxRcntSet();
}

void psxRcntWmode( uint32_t index, uint32_t value )
{
    verboseLog( 1, "[RCNT %i] wmode: %x\n", index, value );

    _psxRcntWmode( index, value );
    _psxRcntWcount( index, 0 );

    rcnts[index].irqState = 0;
    psxRcntSet();
}

void psxRcntWtarget( uint32_t index, uint32_t value )
{
    verboseLog( 1, "[RCNT %i] wtarget: %x\n", index, value );

    rcnts[index].target = value;

    _psxRcntWcount( index, _psxRcntRcount( index ) );
    psxRcntSet();
}

/******************************************************************************/

uint32_t psxRcntRcount( uint32_t index )
{
    uint32_t count;

    count = _psxRcntRcount( index );

    // Parasite Eve 2 fix.
    if( Config.RCntFix ) {
        if( index == 2 ) {
            if( rcnts[index].counterState == CountToTarget )
                count /= BIAS;
        }
    }

    verboseLog( 2, "[RCNT %i] rcount: %x\n", index, count );

    return count;
}

uint32_t psxRcntRmode( uint32_t index )
{
    uint16_t mode;

    mode = rcnts[index].mode;
    rcnts[index].mode &= 0xe7ff;

    verboseLog( 2, "[RCNT %i] rmode: %x\n", index, mode );

    return mode;
}

uint32_t psxRcntRtarget( uint32_t index )
{
    verboseLog( 2, "[RCNT %i] rtarget: %x\n", index, rcnts[index].target );

    return rcnts[index].target;
}

/******************************************************************************/

void psxRcntInit(void)
{
    int32_t i;

    // rcnt 0.
    rcnts[0].rate   = 1;
    rcnts[0].irq    = 0x10;

    // rcnt 1.
    rcnts[1].rate   = 1;
    rcnts[1].irq    = 0x20;

    // rcnt 2.
    rcnts[2].rate   = 1;
    rcnts[2].irq    = 0x40;

    // rcnt base.
    rcnts[3].rate   = 1;
    rcnts[3].mode   = RcCountToTarget;
    rcnts[3].target = (PSXCLK / (FrameRate[Config.PsxType] * HSyncTotal[Config.PsxType]));

    for( i = 0; i < CounterQuantity; ++i )
    {
        _psxRcntWcount( i, 0 );
    }

    hSyncCount = 0;
    hsync_steps = 1;

    psxRcntSet();
}

/******************************************************************************/

/******************************************************************************/

s32 psxRcntFreeze( void *f, s32 Mode )
{
    u32 spuSyncCount = 0;
    u32 count;
    s32 i;

    gzfreeze( &rcnts, sizeof(Rcnt) * CounterQuantity );
    gzfreeze( &hSyncCount, sizeof(hSyncCount) );
    gzfreeze( &spuSyncCount, sizeof(spuSyncCount) );
    gzfreeze( &psxNextCounter, sizeof(psxNextCounter) );
    gzfreeze( &psxNextsCounter, sizeof(psxNextsCounter) );

    if (Mode == 0)
    {
        // don't trust things from a savestate
        for( i = 0; i < CounterQuantity; ++i )
        {
            _psxRcntWmode( i, rcnts[i].mode );
            count = (psxRegs.cycle - rcnts[i].cycleStart) / rcnts[i].rate;
            _psxRcntWcount( i, count );
        }
        hsync_steps = (psxRegs.cycle - rcnts[3].cycleStart) / rcnts[3].target;
        psxRcntSet();

        base_cycle = 0;
    }

    return 0;
}

/******************************************************************************/
// Called before psxRegs.cycle is adjusted back to zero
//  by PSXINT_RESET_CYCLE_VAL event in psxevents.cpp
void psxRcntAdjustTimestamps(const uint32_t prev_cycle_val)
{
	for (int i=0; i < CounterQuantity; ++i) {
		rcnts[i].cycleStart -= prev_cycle_val;
	}
}
