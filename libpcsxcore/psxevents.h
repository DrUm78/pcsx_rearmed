/***************************************************************************
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
 * Event / interrupt scheduling
 *
 * Added July 2016 by senquack (Daniel Silsby)
 *
 */

#ifndef PSXEVENTS_H
#define PSXEVENTS_H

#include <stdio.h>
#include <stdint.h>
#include "r3000a.h"
#include "psxeventnum.h"

void psxEvqueueInit(void);
void psxEvqueueInitFromFreeze(void);
void psxEvqueueAdd(enum psxEventNum ev, uint32_t cycles_after);
void psxEvqueueRemove(enum psxEventNum ev);
void psxEvqueueDispatchAndRemoveFront(psxRegisters *pr);

// Should be called when Config.PsxType changes
void SPU_resetUpdateInterval(void);

#endif //PSXEVENTS_H
