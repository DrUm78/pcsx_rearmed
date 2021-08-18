/***************************************************************************
                            dma.c  -  description
                             -------------------
    begin                : Wed May 15 2002
    copyright            : (C) 2002 by Pete Bernert
    email                : BlackDove@addcom.de
 ***************************************************************************/
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version. See also the license.txt file for *
 *   additional informations.                                              *
 *                                                                         *
 ***************************************************************************/

#include "stdafx.h"

#define _IN_DMA

#include "externals.h"

////////////////////////////////////////////////////////////////////////
// READ DMA (one value)
////////////////////////////////////////////////////////////////////////

unsigned short CALLBACK SPUreadDMA(void)
{
	unsigned short s = (spu.spuMem[spu.spuAddr >> 1]);
	spu.spuAddr = (spu.spuAddr + 2) & 0x7ffff; // Increment SPU address and wrap around
	return s;
}

////////////////////////////////////////////////////////////////////////
// READ DMA (many values)
////////////////////////////////////////////////////////////////////////

void CALLBACK SPUreadDMAMem(unsigned short *pusPSXMem, int iSize,
 unsigned int cycles)
{
	int i;
	do_samples_if_needed(cycles, 1);
	for(i=0;i<iSize;i++)
	{
		*pusPSXMem++ = spu.spuMem[spu.spuAddr >> 1]; // Copy 2 bytes
		spu.spuAddr = (spu.spuAddr + 2) & 0x7ffff; // Increment SPU address and wrap around
	}
}

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

// to investigate: do sound data updates by writedma affect spu
// irqs? Will an irq be triggered, if new data is written to
// the memory irq address?

////////////////////////////////////////////////////////////////////////
// WRITE DMA (one value)
////////////////////////////////////////////////////////////////////////
  
void CALLBACK SPUwriteDMA(unsigned short val)
{
	spu.spuMemC[spu.spuAddr >> 1] = val;
	spu.spuAddr = (spu.spuAddr + 2) & 0x7ffff;  // Increment SPU address and wrap around
	spu.bMemDirty = 1;
}

////////////////////////////////////////////////////////////////////////
// WRITE DMA (many values)
////////////////////////////////////////////////////////////////////////

void CALLBACK SPUwriteDMAMem(unsigned short *pusPSXMem, int iSize,
 unsigned int cycles)
{
	int i;
 
	do_samples_if_needed(cycles, 1);
	spu.bMemDirty = 1;

	if(spu.spuAddr + iSize*2 < 0x80000)
	{
		memcpy(spu.spuMemC + spu.spuAddr, pusPSXMem, iSize*2);
		spu.spuAddr += iSize*2;
		return;
	}

	for(i=0;i<iSize;i++)
	{
		spu.spuMemC[spu.spuAddr >> 1] = *pusPSXMem++;  // Copy 2 bytes
		spu.spuAddr = (spu.spuAddr + 2) & 0x7ffff;  // Increment SPU address and wrap around
	}
}

////////////////////////////////////////////////////////////////////////
