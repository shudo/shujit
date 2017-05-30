/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1998,1999,2000,2003 Kazuyuki Shudo

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

#include <stdio.h>	// for debug
#include <assert.h>	// for assert()


#include "config.h"

#ifdef _WIN32
#  define _JAVASOFT_WIN32_TIMEVAL_H_	// avoid redefinition of timercmp()
#  define _JAVASOFT_WIN32_IO_MD_H_	// avoid redefinition of S_IS*()
#endif
#ifndef HAVE_GREENTHR_HEADER
#  define NATIVE	// for Linux/JDK 1.1.8v1
#endif
#include "sys_api.h"
#undef NATIVE

#include "stack.h"


#define INVALID_ELEM	((long)-1)
#define INITIAL_STACK_SIZE	2


Stack *newStack() {
  Stack *s;
  s = (Stack *)sysMalloc(sizeof(struct collection_stack));
  assert(s != NULL);
  if (s != NULL) {
    s->depth = 0;
    s->capacity = INITIAL_STACK_SIZE;
    s->contents = (long *)sysMalloc(sizeof(long) * INITIAL_STACK_SIZE);
    assert(s->contents != NULL);
  }

  return s;
}


void freeStack(Stack *s) {
  if (s != NULL) {
    if (s->contents != NULL)
      sysFree(s->contents);
    sysFree(s);
  }
}


void clearStack(Stack *s) {
  if (s != NULL) {
    s->depth = 0;
  }
}


void pushToStack(Stack *s, long elem) {
  if (s->depth >= s->capacity) {
    s->capacity <<= 1;
    s->contents = (long *)sysRealloc(s->contents, sizeof(long) * s->capacity);
  }

  s->contents[s->depth++] = elem;
}


long popFromStack(Stack *s) {
  if (s->depth <= 0)  return INVALID_ELEM;
  return s->contents[--s->depth];
}


int stackDepth(Stack *s) {
  return s->depth;
}


long stackElem(Stack *s, int i) {
  if ((i < 0) || (i >= s->depth))
    return INVALID_ELEM;
  else
    return s->contents[i];
}
