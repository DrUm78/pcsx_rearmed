/***************************************************************************
                          adsr.c  -  description
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

#define _IN_ADSR

// will be included from spu.c
#ifdef _IN_SPU

////////////////////////////////////////////////////////////////////////
// ADSR func
////////////////////////////////////////////////////////////////////////

static int RateTableAdd[128];
static int RateTableSub[128];

void InitADSR(void)                                    // INIT ADSR
{
 int lcv, denom;

 // Optimize table - Dr. Hell ADSR math
 for (lcv = 0; lcv < 48; lcv++)
 {
  RateTableAdd[lcv] = (7 - (lcv&3)) << (11 + 16 - (lcv >> 2));
  RateTableSub[lcv] = (-8 + (lcv&3)) << (11 + 16 - (lcv >> 2));
 }

 for (; lcv < 128; lcv++)
 {
  denom = 1 << ((lcv>>2) - 11);

  RateTableAdd[lcv] = ((7 - (lcv&3)) << 16) / denom;
  RateTableSub[lcv] = ((-8 + (lcv&3)) << 16) / denom;

  // XXX: this is wrong, we need more bits..
  if (RateTableAdd[lcv] == 0)
    RateTableAdd[lcv] = 1;
 }
}

////////////////////////////////////////////////////////////////////////

INLINE void StartADSR(int ch)                          // MIX ADSR
{
 spu.s_chan[ch].ADSRX.State = ADSR_ATTACK;             // and init some adsr vars
 spu.s_chan[ch].ADSRX.EnvelopeVol = 0;
}

////////////////////////////////////////////////////////////////////////

static int MixADSR(ADSRInfoEx *adsr, int ns_to)
{
 unsigned int EnvelopeVol = adsr->EnvelopeVol;
 int ns = 0, val, rto, level;

 if (adsr->State == ADSR_RELEASE)
 {
   val = RateTableSub[adsr->ReleaseRate * 4];

   if (adsr->ReleaseModeExp)
   {
     for (; ns < ns_to; ns++)
     {
       EnvelopeVol += ((long long)val * EnvelopeVol) >> (15+16);
       if ((signed int)EnvelopeVol <= 0)
         break;

       ChanBuf[ns] *= (signed int)EnvelopeVol >> 21;
       ChanBuf[ns] >>= 10;
     }
   }
   else
   {
     for (; ns < ns_to; ns++)
     {
       EnvelopeVol += val;
       if ((signed int)EnvelopeVol <= 0)
         break;

       ChanBuf[ns] *= (signed int)EnvelopeVol >> 21;
       ChanBuf[ns] >>= 10;
     }
   }

   goto done;
 }

 switch (adsr->State)
 {
   case ADSR_ATTACK:                                   // -> attack
     rto = 0;
     if (adsr->AttackModeExp && EnvelopeVol >= 0x60000000)
       rto = 8;
     val = RateTableAdd[adsr->AttackRate + rto];

     for (; ns < ns_to; ns++)
     {
       EnvelopeVol += val;
       if ((signed int)EnvelopeVol < 0) // overflow
        break;

       ChanBuf[ns] *= (signed int)EnvelopeVol >> 21;
       ChanBuf[ns] >>= 10;
     }

     if ((signed int)EnvelopeVol < 0) // overflow
     {
       EnvelopeVol = 0x7fffffff;
       adsr->State = ADSR_DECAY;
       ns++; // sample is good already
       goto decay;
     }
     break;

   //--------------------------------------------------//
   decay:
   case ADSR_DECAY:                                    // -> decay
     val = RateTableSub[adsr->DecayRate * 4];
     level = adsr->SustainLevel;

     for (; ns < ns_to; )
     {
       EnvelopeVol += ((long long)val * EnvelopeVol) >> (15+16);
       if ((signed int)EnvelopeVol < 0)
         EnvelopeVol = 0;

       ChanBuf[ns] *= EnvelopeVol >> 21;
       ChanBuf[ns] >>= 10;
       ns++;

       if (((EnvelopeVol >> 27) & 0xf) <= level)
       {
         adsr->State = ADSR_SUSTAIN;
         goto sustain;
       }
     }
     break;

   //--------------------------------------------------//
   sustain:
   case ADSR_SUSTAIN:                                  // -> sustain
     if (adsr->SustainIncrease)
     {
       if (EnvelopeVol >= 0x7fff0000)
       {
         ns = ns_to;
         break;
       }

       rto = 0;
       if (adsr->SustainModeExp && EnvelopeVol >= 0x60000000)
         rto = 8;
       val = RateTableAdd[adsr->SustainRate + rto];

       for (; ns < ns_to; ns++)
       {
         EnvelopeVol += val;
         if (EnvelopeVol >= 0x7fe00000)
         {
           EnvelopeVol = 0x7fffffff;
           ns = ns_to;
           break;
         }

         ChanBuf[ns] *= (signed int)EnvelopeVol >> 21;
         ChanBuf[ns] >>= 10;
       }
     }
     else
     {
       val = RateTableSub[adsr->SustainRate];
       if (adsr->SustainModeExp)
       {
         for (; ns < ns_to; ns++)
         {
           EnvelopeVol += ((long long)val * EnvelopeVol) >> (15+16);
           if ((signed int)EnvelopeVol < 0)
             break;

           ChanBuf[ns] *= (signed int)EnvelopeVol >> 21;
           ChanBuf[ns] >>= 10;
         }
       }
       else
       {
         for (; ns < ns_to; ns++)
         {
           EnvelopeVol += val;
           if ((signed int)EnvelopeVol < 0)
             break;

           ChanBuf[ns] *= (signed int)EnvelopeVol >> 21;
           ChanBuf[ns] >>= 10;
         }
       }
     }
     break;
 }

done:
 adsr->EnvelopeVol = EnvelopeVol;
 return ns;
}

static int SkipADSR(ADSRInfoEx *adsr, int ns_to)
{
 unsigned int EnvelopeVol = adsr->EnvelopeVol;
 int ns = 0, val, rto, level;
 int64_t v64;

 if (adsr->State == ADSR_RELEASE)
 {
   val = RateTableSub[adsr->ReleaseRate * 4];
   if (adsr->ReleaseModeExp)
   {
     for (; ns < ns_to; ns++)
     {
       EnvelopeVol += ((long long)val * EnvelopeVol) >> (15+16);
       if ((signed int)EnvelopeVol <= 0)
         break;
     }
   }
   else
   {
     v64 = EnvelopeVol;
     v64 += (int64_t)val * ns_to;
     EnvelopeVol = (int)v64;
     if (v64 > 0)
       ns = ns_to;
   }
   goto done;
 }

 switch (adsr->State)
 {
   case ADSR_ATTACK:                                   // -> attack
     rto = 0;
     if (adsr->AttackModeExp && EnvelopeVol >= 0x60000000)
       rto = 8;
     val = RateTableAdd[adsr->AttackRate + rto];

     for (; ns < ns_to; ns++)
     {
       EnvelopeVol += val;
       if ((signed int)EnvelopeVol < 0)
        break;
     }
     if ((signed int)EnvelopeVol < 0) // overflow
     {
       EnvelopeVol = 0x7fffffff;
       adsr->State = ADSR_DECAY;
       ns++;
       goto decay;
     }
     break;

   //--------------------------------------------------//
   decay:
   case ADSR_DECAY:                                    // -> decay
     val = RateTableSub[adsr->DecayRate * 4];
     level = adsr->SustainLevel;

     for (; ns < ns_to; )
     {
       EnvelopeVol += ((long long)val * EnvelopeVol) >> (15+16);
       if ((signed int)EnvelopeVol < 0)
         EnvelopeVol = 0;

       ns++;

       if (((EnvelopeVol >> 27) & 0xf) <= level)
       {
         adsr->State = ADSR_SUSTAIN;
         goto sustain;
       }
     }
     break;

   //--------------------------------------------------//
   sustain:
   case ADSR_SUSTAIN:                                  // -> sustain
     if (adsr->SustainIncrease)
     {
       ns = ns_to;

       if (EnvelopeVol >= 0x7fff0000)
         break;

       rto = 0;
       if (adsr->SustainModeExp && EnvelopeVol >= 0x60000000)
         rto = 8;
       val = RateTableAdd[adsr->SustainRate + rto];

       v64 = EnvelopeVol;
       v64 += (int64_t)val * (ns_to - ns);
       EnvelopeVol = (int)v64;
       if (v64 >= 0x7fe00000ll)
         EnvelopeVol = 0x7fffffff;
     }
     else
     {
       val = RateTableSub[adsr->SustainRate];
       if (adsr->SustainModeExp)
       {
         for (; ns < ns_to; ns++)
         {
           EnvelopeVol += ((long long)val * EnvelopeVol) >> (15+16);
           if ((signed int)EnvelopeVol < 0)
             break;
         }
       }
       else
       {
         v64 = EnvelopeVol;
         v64 += (int64_t)val * (ns_to - ns);
         EnvelopeVol = (int)v64;
         if (v64 > 0)
           ns = ns_to;
       }
     }
     break;
 }

done:
 adsr->EnvelopeVol = EnvelopeVol;
 return ns;
}

#endif

/*
James Higgs ADSR investigations:

PSX SPU Envelope Timings
~~~~~~~~~~~~~~~~~~~~~~~~

First, here is an extract from doomed's SPU doc, which explains the basics
of the SPU "volume envelope": 

*** doomed doc extract start ***

--------------------------------------------------------------------------
Voices.
--------------------------------------------------------------------------
The SPU has 24 hardware voices. These voices can be used to reproduce sample
data, noise or can be used as frequency modulator on the next voice.
Each voice has it's own programmable ADSR envelope filter. The main volume
can be programmed independently for left and right output.

The ADSR envelope filter works as follows:
Ar = Attack rate, which specifies the speed at which the volume increases
     from zero to it's maximum value, as soon as the note on is given. The
     slope can be set to lineair or exponential.
Dr = Decay rate specifies the speed at which the volume decreases to the
     sustain level. Decay is always decreasing exponentially.
Sl = Sustain level, base level from which sustain starts.
Sr = Sustain rate is the rate at which the volume of the sustained note
     increases or decreases. This can be either lineair or exponential.
Rr = Release rate is the rate at which the volume of the note decreases
     as soon as the note off is given.

     lvl |
       ^ |     /\Dr     __
     Sl _| _  / _ \__---  \
         |   /       ---__ \ Rr
         |  /Ar       Sr  \ \
         | /                \\
         |/___________________\________
                                  ->time

The overal volume can also be set to sweep up or down lineairly or
exponentially from it's current value. This can be done seperately
for left and right.

Relevant SPU registers:
-------------------------------------------------------------
$1f801xx8         Attack/Decay/Sustain level
bit  |0f|0e 0d 0c 0b 0a 09 08|07 06 05 04|03 02 01 00|
desc.|Am|         Ar         |Dr         |Sl         |

Am       0        Attack mode Linear
         1                    Exponential

Ar       0-7f     attack rate
Dr       0-f      decay rate
Sl       0-f      sustain level
-------------------------------------------------------------
$1f801xxa         Sustain rate, Release Rate.
bit  |0f|0e|0d|0c 0b 0a 09 08 07 06|05|04 03 02 01 00|
desc.|Sm|Sd| 0|   Sr               |Rm|Rr            |

Sm       0        sustain rate mode linear
         1                          exponential
Sd       0        sustain rate mode increase
         1                          decrease
Sr       0-7f     Sustain Rate
Rm       0        Linear decrease
         1        Exponential decrease
Rr       0-1f     Release Rate

Note: decay mode is always Expontial decrease, and thus cannot
be set.
-------------------------------------------------------------
$1f801xxc         Current ADSR volume
bit  |0f 0e 0d 0c 0b 0a 09 08 07 06 05 04 03 02 01 00|
desc.|ADSRvol                                        |

ADSRvol           Returns the current envelope volume when
                  read.
-- James' Note: return range: 0 -> 32767

*** doomed doc extract end *** 

By using a small PSX proggie to visualise the envelope as it was played,
the following results for envelope timing were obtained:

1. Attack rate value (linear mode)

   Attack value range: 0 -> 127

   Value  | 48 | 52 | 56 | 60 | 64 | 68 | 72 |    | 80 |
   -----------------------------------------------------------------
   Frames | 11 | 21 | 42 | 84 | 169| 338| 676|    |2890|

   Note: frames is no. of PAL frames to reach full volume (100%
   amplitude)

   Hmm, noticing that the time taken to reach full volume doubles
   every time we add 4 to our attack value, we know the equation is
   of form:
             frames = k * 2 ^ (value / 4)

   (You may ponder about envelope generator hardware at this point,
   or maybe not... :)

   By substituting some stuff and running some checks, we get:

       k = 0.00257              (close enuf)

   therefore,
             frames = 0.00257 * 2 ^ (value / 4)
   If you just happen to be writing an emulator, then you can probably
   use an equation like:

       %volume_increase_per_tick = 1 / frames


   ------------------------------------
   Pete:
   ms=((1<<(value>>2))*514)/10000
   ------------------------------------

2. Decay rate value (only has log mode)

   Decay value range: 0 -> 15

   Value  |  8 |  9 | 10 | 11 | 12 | 13 | 14 | 15 |
   ------------------------------------------------
   frames |    |    |    |    |  6 | 12 | 24 | 47 |

   Note: frames here is no. of PAL frames to decay to 50% volume.

   formula: frames = k * 2 ^ (value)

   Substituting, we get: k = 0.00146

   Further info on logarithmic nature:
   frames to decay to sustain level 3  =  3 * frames to decay to 
   sustain level 9

   Also no. of frames to 25% volume = roughly 1.85 * no. of frames to
   50% volume.

   Frag it - just use linear approx.

   ------------------------------------
   Pete:
   ms=((1<<value)*292)/10000
   ------------------------------------


3. Sustain rate value (linear mode)

   Sustain rate range: 0 -> 127

   Value  | 48 | 52 | 56 | 60 | 64 | 68 | 72 |
   -------------------------------------------
   frames |  9 | 19 | 37 | 74 | 147| 293| 587|

   Here, frames = no. of PAL frames for volume amplitude to go from 100%
   to 0% (or vice-versa).

   Same formula as for attack value, just a different value for k:

   k = 0.00225

   ie: frames = 0.00225 * 2 ^ (value / 4)

   For emulation purposes:

   %volume_increase_or_decrease_per_tick = 1 / frames

   ------------------------------------
   Pete:
   ms=((1<<(value>>2))*450)/10000
   ------------------------------------


4. Release rate (linear mode)

   Release rate range: 0 -> 31

   Value  | 13 | 14 | 15 | 16 | 17 |
   ---------------------------------------------------------------
   frames | 18 | 36 | 73 | 146| 292|

   Here, frames = no. of PAL frames to decay from 100% vol to 0% vol
   after "note-off" is triggered.

   Formula: frames = k * 2 ^ (value)

   And so: k = 0.00223

   ------------------------------------
   Pete:
   ms=((1<<value)*446)/10000
   ------------------------------------


Other notes:   

Log stuff not figured out. You may get some clues from the "Decay rate"
stuff above. For emu purposes it may not be important - use linear
approx.

To get timings in millisecs, multiply frames by 20.



- James Higgs 17/6/2000
james7780@yahoo.com

//---------------------------------------------------------------

OLD adsr mixing according to james' rules... has to be called
every one millisecond


 long v,v2,lT,l1,l2,l3;

 if(s_chan[ch].bStop)                                  // psx wants to stop? -> release phase
  {
   if(s_chan[ch].ADSR.ReleaseVal!=0)                   // -> release not 0: do release (if 0: stop right now)
    {
     if(!s_chan[ch].ADSR.ReleaseVol)                   // --> release just started? set up the release stuff
      {
       s_chan[ch].ADSR.ReleaseStartTime=s_chan[ch].ADSR.lTime;
       s_chan[ch].ADSR.ReleaseVol=s_chan[ch].ADSR.lVolume;
       s_chan[ch].ADSR.ReleaseTime =                   // --> calc how long does it take to reach the wanted sus level
         (s_chan[ch].ADSR.ReleaseTime*
          s_chan[ch].ADSR.ReleaseVol)/1024;
      }
                                                       // -> NO release exp mode used (yet)
     v=s_chan[ch].ADSR.ReleaseVol;                     // -> get last volume
     lT=s_chan[ch].ADSR.lTime-                         // -> how much time is past?
        s_chan[ch].ADSR.ReleaseStartTime;
     l1=s_chan[ch].ADSR.ReleaseTime;
                                                       
     if(lT<l1)                                         // -> we still have to release
      {
       v=v-((v*lT)/l1);                                // --> calc new volume
      }
     else                                              // -> release is over: now really stop that sample
      {v=0;s_chan[ch].bOn=0;s_chan[ch].ADSR.ReleaseVol=0;s_chan[ch].bNoise=0;}
    }
   else                                                // -> release IS 0: release at once
    {
     v=0;s_chan[ch].bOn=0;s_chan[ch].ADSR.ReleaseVol=0;s_chan[ch].bNoise=0;
    }
  }
 else                                               
  {//--------------------------------------------------// not in release phase:
   v=1024;
   lT=s_chan[ch].ADSR.lTime;
   l1=s_chan[ch].ADSR.AttackTime;
                                                       
   if(lT<l1)                                           // attack
    {                                                  // no exp mode used (yet)
//     if(s_chan[ch].ADSR.AttackModeExp)
//      {
//       v=(v*lT)/l1;
//      }
//     else
      {
       v=(v*lT)/l1;
      }
     if(v==0) v=1;
    }
   else                                                // decay
    {                                                  // should be exp, but who cares? ;)
     l2=s_chan[ch].ADSR.DecayTime;
     v2=s_chan[ch].ADSR.SustainLevel;

     lT-=l1;
     if(lT<l2)
      {
       v-=(((v-v2)*lT)/l2);
      }
     else                                              // sustain
      {                                                // no exp mode used (yet)
       l3=s_chan[ch].ADSR.SustainTime;
       lT-=l2;
       if(s_chan[ch].ADSR.SustainModeDec>0)
        {
         if(l3!=0) v2+=((v-v2)*lT)/l3;
         else      v2=v;
        }
       else
        {
         if(l3!=0) v2-=(v2*lT)/l3;
         else      v2=v;
        }

       if(v2>v)  v2=v;
       if(v2<=0) {v2=0;s_chan[ch].bOn=0;s_chan[ch].ADSR.ReleaseVol=0;s_chan[ch].bNoise=0;}

       v=v2;
      }
    }
  }

 //----------------------------------------------------// 
 // ok, done for this channel, so increase time

 s_chan[ch].ADSR.lTime+=1;                             // 1 = 1.020408f ms;      

 if(v>1024)     v=1024;                                // adjust volume
 if(v<0)        v=0;                                  
 s_chan[ch].ADSR.lVolume=v;                            // store act volume

 return v;                                             // return the volume factor
*/


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/*
-----------------------------------------------------------------------------
Neill Corlett
Playstation SPU envelope timing notes
-----------------------------------------------------------------------------

This is preliminary.  This may be wrong.  But the model described herein fits
all of my experimental data, and it's just simple enough to sound right.

ADSR envelope level ranges from 0x00000000 to 0x7FFFFFFF internally.
The value returned by channel reg 0xC is (envelope_level>>16).

Each sample, an increment or decrement value will be added to or
subtracted from this envelope level.

Create the rate log table.  The values double every 4 entries.
   entry #0 = 4

    4, 5, 6, 7,
    8,10,12,14,
   16,20,24,28, ...

   entry #40 = 4096...
   entry #44 = 8192...
   entry #48 = 16384...
   entry #52 = 32768...
   entry #56 = 65536...

increments and decrements are in terms of ratelogtable[n]
n may exceed the table bounds (plan on n being between -32 and 127).
table values are all clipped between 0x00000000 and 0x3FFFFFFF

when you "voice on", the envelope is always fully reset.
(yes, it may click. the real thing does this too.)

envelope level begins at zero.

each state happens for at least 1 cycle
(transitions are not instantaneous)
this may result in some oddness: if the decay rate is uberfast, it will cut
the envelope from full down to half in one sample, potentially skipping over
the sustain level

ATTACK
------
- if the envelope level has overflowed past the max, clip to 0x7FFFFFFF and
  proceed to DECAY.

Linear attack mode:
- line extends upward to 0x7FFFFFFF
- increment per sample is ratelogtable[(Ar^0x7F)-0x10]

Logarithmic attack mode:
if envelope_level < 0x60000000:
  - line extends upward to 0x60000000
  - increment per sample is ratelogtable[(Ar^0x7F)-0x10]
else:
  - line extends upward to 0x7FFFFFFF
  - increment per sample is ratelogtable[(Ar^0x7F)-0x18]

DECAY
-----
- if ((envelope_level>>27)&0xF) <= Sl, proceed to SUSTAIN.
  Do not clip to the sustain level.
- current line ends at (envelope_level & 0x07FFFFFF)
- decrement per sample depends on (envelope_level>>28)&0x7
  0: ratelogtable[(4*(Dr^0x1F))-0x18+0]
  1: ratelogtable[(4*(Dr^0x1F))-0x18+4]
  2: ratelogtable[(4*(Dr^0x1F))-0x18+6]
  3: ratelogtable[(4*(Dr^0x1F))-0x18+8]
  4: ratelogtable[(4*(Dr^0x1F))-0x18+9]
  5: ratelogtable[(4*(Dr^0x1F))-0x18+10]
  6: ratelogtable[(4*(Dr^0x1F))-0x18+11]
  7: ratelogtable[(4*(Dr^0x1F))-0x18+12]
  (note that this is the same as the release rate formula, except that
   decay rates 10-1F aren't possible... those would be slower in theory)

SUSTAIN
-------
- no terminating condition except for voice off
- Sd=0 (increase) behavior is identical to ATTACK for both log and linear.
- Sd=1 (decrease) behavior:
Linear sustain decrease:
- line extends to 0x00000000
- decrement per sample is ratelogtable[(Sr^0x7F)-0x0F]
Logarithmic sustain decrease:
- current line ends at (envelope_level & 0x07FFFFFF)
- decrement per sample depends on (envelope_level>>28)&0x7
  0: ratelogtable[(Sr^0x7F)-0x1B+0]
  1: ratelogtable[(Sr^0x7F)-0x1B+4]
  2: ratelogtable[(Sr^0x7F)-0x1B+6]
  3: ratelogtable[(Sr^0x7F)-0x1B+8]
  4: ratelogtable[(Sr^0x7F)-0x1B+9]
  5: ratelogtable[(Sr^0x7F)-0x1B+10]
  6: ratelogtable[(Sr^0x7F)-0x1B+11]
  7: ratelogtable[(Sr^0x7F)-0x1B+12]

RELEASE
-------
- if the envelope level has overflowed to negative, clip to 0 and QUIT.

Linear release mode:
- line extends to 0x00000000
- decrement per sample is ratelogtable[(4*(Rr^0x1F))-0x0C]

Logarithmic release mode:
- line extends to (envelope_level & 0x0FFFFFFF)
- decrement per sample depends on (envelope_level>>28)&0x7
  0: ratelogtable[(4*(Rr^0x1F))-0x18+0]
  1: ratelogtable[(4*(Rr^0x1F))-0x18+4]
  2: ratelogtable[(4*(Rr^0x1F))-0x18+6]
  3: ratelogtable[(4*(Rr^0x1F))-0x18+8]
  4: ratelogtable[(4*(Rr^0x1F))-0x18+9]
  5: ratelogtable[(4*(Rr^0x1F))-0x18+10]
  6: ratelogtable[(4*(Rr^0x1F))-0x18+11]
  7: ratelogtable[(4*(Rr^0x1F))-0x18+12]

-----------------------------------------------------------------------------
*/

// vim:shiftwidth=1:expandtab
