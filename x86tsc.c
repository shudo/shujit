/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1998,1999,2000,2005 Kazuyuki Shudo

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  $Id$
*/

// utilities for x86 Time Stamp Counter

#include "x86tsc.h"

static unsigned long sh, sl, eh, el;
static unsigned long long overhead = 0;


inline unsigned long long int rdtsc() {
  unsigned long long int tsc;
  asm(".byte 0x0f,0x31" : "=A" (tsc));
  return tsc;
}


void tscStart() {
  tscEnd();	// preload tscEnd to code cache
#ifndef SUPRESS_PRELOAD
  // preload sh,sl to data cache
  asm("movl  %0,%%edx\n\t"
      "movl  %1,%%eax"
	: : "m" (sh), "m" (sl) : "eax","edx");
#endif

#ifdef DISABLE_INTR
  asm("cli");
#endif
  asm(".byte 0x0f,0x31");
  asm("movl  %%edx,%0\n\t"
      "movl  %%eax,%1"
	: : "m" (sh), "m" (sl) : "eax","edx");
}


void tscEnd() {
  asm(".byte 0x0f,0x31");
#ifdef DISABLE_INTR
  asm("sti");
#endif
  asm("movl  %%edx,%0\n\t"
      "movl  %%eax,%1"
	: : "m" (eh), "m" (el));
}


unsigned long long tscClock() {
  unsigned long long s,e;

  *((unsigned long *)&s) = sl;
  *((unsigned long *)&s + 1) = sh;
  *((unsigned long *)&e) = el;
  *((unsigned long *)&e + 1) = eh;

  e -= (s + overhead);
  return e;
}
