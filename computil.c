/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1998,1999,2000,2001,2002 Kazuyuki Shudo

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

#include <stdio.h>
#include <stdlib.h>	// for malloc(), realloc()
#include <string.h>	// for memcpy()

#include "compiler.h"


static CompilerContext *cc_pool = NULL;

static void resetCompilerContext(CompilerContext *cc, struct methodblock *mb);
static CompilerContext *newCompilerContext(struct methodblock *mb);
static void freeCompilerContext(CompilerContext *cc);


void *access2invoker(int access) {
  if (access & ACC_ABSTRACT) {
    return (void *)sym_invokeAbstractMethod;
  }
  else if (access & ACC_NATIVE) {
    return (void *)sym_invokeLazyNativeMethod;
  }
  else {
    if (access & ACC_SYNCHRONIZED) {
      return (void *)sym_invokeSynchronizedJavaMethod;
    }
    else {
      return (void *)sym_invokeJavaMethod;
    }
  }
}


char *nameOfInvoker(void *inv) {
  char *ret;

  if (!inv)  return "(null)";

#define IF_A_INVOKER(NAME)	if (inv == sym_##NAME)  ret = #NAME
#define ELIF_A_INVOKER(NAME)	else IF_A_INVOKER(NAME)

  IF_A_INVOKER(compileAndInvokeMethod);
  ELIF_A_INVOKER(invokeJITCompiledMethod);
  ELIF_A_INVOKER(invokeJavaMethod);
  ELIF_A_INVOKER(invokeSynchronizedJavaMethod);
  ELIF_A_INVOKER(invokeAbstractMethod);
  ELIF_A_INVOKER(invokeNativeMethod);
  ELIF_A_INVOKER(invokeSynchronizedNativeMethod);
  ELIF_A_INVOKER(invokeJNINativeMethod);
  ELIF_A_INVOKER(invokeJNISynchronizedNativeMethod);
  ELIF_A_INVOKER(invokeLazyNativeMethod);
#if JDK_VER >= 12
  else if (((uint32_t)inv >= sym_invokeJNI_min) &&
	   ((uint32_t)inv <= sym_invokeJNI_max)) {
    ret = "<a custom invoker for invokeJNINativeMethod>";
  }
#endif
  else  ret = "<other>";

  return ret;
}


void showCompilerContext(CompilerContext *cc, char *prefix) {
  printf("%scompiler context: 0x%x\n", prefix, (int)cc);
  fflush(stdout);

  if (cc) {
    struct methodblock *mb = cc->mb;

    printf("%s  ExecEnv: 0x%08x\n", prefix, (int)cc->ee);
    printf("%s  method: %s#%s %s (0x%08x)\n", prefix,
	(mb ? cbName(fieldclass(&mb->fb)) : "(null)"),
	(mb ? mb->fb.name : "(null)"),
	(mb ? mb->fb.signature : "(null)"), (int)mb);
    printf("%s  buffer: 0x%08x\n", prefix, (int)cc->buffer);
    printf("%s  bufp  : 0x%08x offset:0x%x(%d)\n", prefix,
	(int)cc->bufp, cc->bufp - cc->buffer, cc->bufp - cc->buffer);
    printf("%s  pctable: 0x%08x\n", prefix, cc->pctable);
    printf("%s  jptable: 0x%08x\n", prefix, cc->jptable);
  }
  fflush(stdout);
}


CompilerContext *getCompilerContext(struct methodblock *mb) {
  ExecEnv *ee = EE();
  CompilerContext *cc;
  CodeInfo *info;
#if JDK_VER >= 12
  sys_thread_t *self = EE2SysThread(ee);
#endif
  sys_mon_t *mon;

  // examine whether already created
  SYS_MONITOR_ENTER(self, global_monitor);
  info = (CodeInfo *)mb->CompiledCodeInfo;
  if (info == NULL) {
    info = prepareCompiledCodeInfo(ee, mb);
  }
  SYS_MONITOR_EXIT(self, global_monitor);

  mon = info->monitor;

  SYS_MONITOR_ENTER(self, mon);

  cc = info->cc;
  if (cc == NULL) {
    // prepare new cc
    if (cc_pool) {
      cc = cc_pool;
      cc_pool = cc_pool->next;

      resetCompilerContext(cc, mb);
    }
    else {
      cc = newCompilerContext(mb);
    }
  }

  cc->ee = ee;	// set ExecEnv
  cc->ref_count++;

  SYS_MONITOR_EXIT(self, mon);

  return cc;
}


void releaseCompilerContext(CompilerContext *cc) {
  CodeInfo *info;
#if JDK_VER >= 12
  ExecEnv *ee = EE();
  sys_thread_t *self = EE2SysThread(ee);
#endif
  sys_mon_t *mon;

  SYS_MONITOR_ENTER(self, global_monitor);

  cc->ref_count--;
  if (cc->ref_count <= 0) {
    // release
    info = (CodeInfo *)cc->mb->CompiledCodeInfo;
    if (info != NULL) {
      info->cc = NULL;
    }

    cc->next = cc_pool;
    cc_pool = cc;
  }

  SYS_MONITOR_EXIT(self, global_monitor);
}


static void resetCompilerContext(CompilerContext *cc, struct methodblock *mb) {
  CodeInfo *info;
#ifdef COMPILE_DEBUG
  cc->compile_debug = debugp(mb);
#endif

#ifdef COMPILE_DEBUG
  if (cc->compile_debug) {
    printf("resetCompilerContext(): mb is 0x%08x", (int)mb);
    if (mb != NULL) {
      printf("(%s#%s %s)",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
    }
    else {
      printf("(null)");
    }
    printf("\n");
    fflush(stdout);
  }
#endif

  cc->ee = EE();
  cc->mb = mb;
  cc->ref_count = 0;

  cc->stage = STAGE_START;
  cc->may_throw = FALSE;
  cc->may_jump = FALSE;

  cc->bufp = cc->buffer;

  pctableClear(cc);
  cc->jptablelen = 0;

  cc->next = NULL;

#ifdef EAGER_COMPILATION
  // set the new cc to the codeinfo
  info = (CodeInfo *)mb->CompiledCodeInfo;
  if (info != NULL) {
    info->cc = cc;
  }
#endif
}


static CompilerContext *newCompilerContext(struct methodblock *mb) {
  CompilerContext *cc;

  cc = (CompilerContext *)sysMalloc(sizeof(CompilerContext));
  if (!cc) {
#ifdef COMPILE_DEBUG
    printf("newCompilerContext(): cannot allocate memory.\n");
    fflush(stdout);
#endif
    return NULL;
  }

  cc->buffer = (unsigned char *)sysMalloc(DEFAULT_BUF_SIZE);
  cc->buf_size = DEFAULT_BUF_SIZE;

  cc->pctablesize = DEFAULT_PCTABLE_SIZE;
  cc->pctable = (pcentry *)sysMalloc(sizeof(pcentry) * DEFAULT_PCTABLE_SIZE);

  cc->jptablesize = DEFAULT_JPTABLE_SIZE;
  cc->jptable = (jpentry *)sysMalloc(sizeof(jpentry) * DEFAULT_JPTABLE_SIZE);

  resetCompilerContext(cc, mb);

  return cc;
}


static void freeCompilerContext(CompilerContext *cc) {
#ifdef COMPILE_DEBUG
  printf("freeCompilerContext(): cc is 0x%08x\n", (int)cc);
  fflush(stdout);
#endif
  if (cc) {
#ifdef COMPILE_DEBUG
    printf("  buffer: 0x%08x\n  pctable: 0x%08x\n  jptable: 0x%08x\n",
	(int)cc->buffer, (int)cc->pctable, (int)cc->jptable);
    fflush(stdout);
#endif
    if (cc->buffer)  sysFree(cc->buffer);
    if (cc->pctable)  sysFree(cc->pctable);
    if (cc->jptable)  sysFree(cc->jptable);
    sysFree(cc);
#ifdef COMPILE_DEBUG
    fflush(stdout);
    fflush(stderr);
#endif
  }
#ifdef COMPILE_DEBUG
  printf("freeCompilerContext() done.\n");
  fflush(stdout);
#endif
}


inline void ensureBufferSize(CompilerContext *cc, size_t req) {
  int buf_offset = cc->bufp - cc->buffer;

  if (req > (cc->buf_size - buf_offset)) {

#ifdef BUF_DEBUG
    printf("ensureBufferSize(): now extend the space.\n");
    printf("  buffer 0x%x, bufp 0x%x, offset 0x%x(%d)\n",
		(int)cc->buffer, (int)cc->bufp, buf_offset, buf_offset);
    fflush(stdout);
#endif
#ifdef BUF_DEBUG
    printf("  buf_size: %d -> ", cc->buf_size);
#endif
    do {
      cc->buf_size <<= 1;
    } while (req > (cc->buf_size - buf_offset));
#ifdef BUF_DEBUG
    printf("%d.\n", cc->buf_size);
    fflush(stdout);
#endif

    cc->buffer = (unsigned char *)sysRealloc(cc->buffer, cc->buf_size);
    cc->bufp = cc->buffer + buf_offset;
  }
}


void writeToBuffer(CompilerContext *cc, void *ptr, size_t len) {
#ifdef BUF_DEBUG
  printf("writeToBuffer(cc, 0x%x, %d) called.\n", (int)ptr, (int)len);
  printf("  bufp 0x%x(offset:%d)\n", (int)cc->bufp, cc->bufp - cc->buffer);
  fflush(stdout);
#endif
  ensureBufferSize(cc, len);
  memcpy(cc->bufp, ptr, len);

  cc->bufp += len;
#ifdef BUF_DEBUG
  printf("writeToBuffer() done.\n");
  fflush(stdout);
#endif
}


// PC table

#ifdef CODE_DB
void pctableExtend(CompilerContext *cc, uint32_t size) {
  if (size < cc->pctablesize)  return;

  cc->pctable = (pcentry *)sysRealloc(cc->pctable, sizeof(pcentry) * size);
  cc->pctablesize = size;
}
#endif	// CODE_DB


void pctableClear(CompilerContext *cc) {
  cc->ninsn = cc->pctablelen = 0;
}


inline uint32_t pctableLen(CompilerContext *cc) {
  return cc->pctablelen;
}


void pctableSetLen(CompilerContext *cc, uint32_t len) {
  cc->pctablelen = len;
}


void pctableAdd(CompilerContext *cc,
	int opcode, int operand, unsigned int byteoff) {
  pcentry *entryp;

  if (cc->pctablelen >= cc->pctablesize) {	// extend table size
    do
      cc->pctablesize <<= 1;
    while (cc->pctablelen >= cc->pctablesize);
    cc->pctable = (pcentry *)sysRealloc(cc->pctable,
				sizeof(pcentry) * cc->pctablesize);
  }

  entryp = cc->pctable + cc->pctablelen;

  entryp->opcode = opcode;
  entryp->operand = operand;
  entryp->byteoff = entryp->increasing_byteoff = byteoff;

  pcentrySetState(entryp, 0);
  pcentryClearBlockHead(entryp);
  pcentryClearLoopHead(entryp);
  pcentryClearLoopTail(entryp);
  entryp->nativeoff = -1;

  cc->pctablelen++;
}


void pctableNInsert(CompilerContext *cc, int index,
	pcentry *srcentry, int srclen) {
  pcentry *entryp;
  int origLen, futureLen;
  int i;

  origLen = cc->pctablelen;
  futureLen = cc->pctablelen + srclen;
  if (futureLen > cc->pctablesize) {
    // extend the table
    do {
      cc->pctablesize <<= 1;
    } while (futureLen > cc->pctablesize);
    cc->pctable = (pcentry *)sysRealloc(cc->pctable,
				sizeof(pcentry) * cc->pctablesize);
  }

  entryp = cc->pctable + index;
  memmove(entryp + srclen, entryp, sizeof(pcentry) * (origLen - index));

  memcpy(entryp, srcentry, sizeof(pcentry) * srclen);

  cc->pctablelen += srclen;
}


pcentry *pctableInsert(CompilerContext *cc, int index,
	int opcode, int operand, int32_t byteoff,
	int state, int nativeoff) {
  pcentry *entryp;

  if (cc->pctablelen >= cc->pctablesize) {
    // extend the table
    do {
      cc->pctablesize <<= 1;
    } while ((cc->pctablelen + 1) > cc->pctablesize);
    cc->pctable = (pcentry *)sysRealloc(cc->pctable,
				sizeof(pcentry) * cc->pctablesize);
  }

  entryp = cc->pctable + index;
  memmove(entryp + 1, entryp, sizeof(pcentry) * (cc->pctablelen - index));

  entryp->opcode = opcode;
  entryp->operand = operand;
  entryp->byteoff = entryp->increasing_byteoff = byteoff;
  pcentrySetState(entryp, state);
  entryp->nativeoff = nativeoff;

  pcentryClearBlockHead(entryp);
  pcentryClearLoopHead(entryp);
  pcentryClearLoopTail(entryp);

  cc->pctablelen++;

  return entryp;
}


void pctableNDelete(CompilerContext *cc, int index, int len) {
  pcentry *entryp = cc->pctable + index;

  memmove(entryp, entryp + len,
	sizeof(pcentry) * (cc->pctablelen - index - len));
  (cc->pctablelen) -= len;
}


void pctableDelete(CompilerContext *cc, int index) {
  pctableNDelete(cc, index, 1);
}


pcentry *pctableNext(CompilerContext *cc, pcentry *entry) {
  if ((entry - cc->pctable) + 1 < cc->pctablelen)
    return entry + 1;
  else
    return NULL;
}


pcentry *pctableGet(CompilerContext *cc, int index) {
  if ((index < 0) || (index >= cc->pctablelen))
    return NULL;
  else
    return cc->pctable + index;
}


pcentry *pctableGetByPC(CompilerContext *cc, int32_t byteoff) {
  int l = 0;
  int h = cc->pctablelen;
  int index;
  pcentry *entryp;

  while (1) {
    index = l + ((h - l) >> 1);

    entryp = cc->pctable + index;
    if (entryp->increasing_byteoff == byteoff) {	// found
      while (--index >= 0) {
	if ((cc->pctable + index)->increasing_byteoff != byteoff)
	  break;
      }
      index++;
      return cc->pctable + index;
    }

    if (l == h) {	// not found
#ifdef COMPILE_DEBUG
      struct methodblock *mb = cc->mb;
      printf("FATAL: pctableGetByPC(%s#%s %s)\n",
		cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
      printf("  not found: 0x%x(%d)\n", byteoff, byteoff);
      fflush(stdout);
#endif
      return NULL;
    }

    if (entryp->increasing_byteoff < byteoff)
      l = index + 1;
    else
      h = index;
  }
}


void pcentryClear(pcentry *entry) {
  entry->opcode = (uint16_t)0;
  entry->operand = -1;
  entry->byteoff = entry->increasing_byteoff = -1;
  pcentrySetState(entry, 0);
  pcentryClearBlockHead(entry);
  pcentryClearLoopHead(entry);
  pcentryClearLoopTail(entry);
  entry->nativeoff = -1;
}


// Jump instruction table

void jptableAdd(CompilerContext *cc,
	unsigned int tgtoff, unsigned int argoff) {
  jpentry *entryp;
#ifdef COMPILE_DEBUG
  int compile_debug = cc->compile_debug;
#endif

  if (cc->jptablelen >= cc->jptablesize) {	// extend table size
#ifdef COMPILE_DEBUG
    if (compile_debug) {
      printf("jptableAdd(): extending jump table.\n");  fflush(stdout);
    }
#endif
    do {
      cc->jptablesize <<= 1;
    } while (cc->jptablelen >= cc->jptablesize);

    cc->jptable = (jpentry *)sysRealloc(cc->jptable,
				sizeof(jpentry) * cc->jptablesize);
#ifdef COMPILE_DEBUG
    if (compile_debug) {
      printf("jptableAdd() extending done: 0x%x\n", (int)(cc->jptable));
      fflush(stdout);
    }
#endif
  }

  entryp = cc->jptable + cc->jptablelen;
  entryp->tgtoff = tgtoff;
  entryp->argoff = argoff;

  (cc->jptablelen)++;
}


CodeInfo *prepareCompiledCodeInfo(ExecEnv *ee, struct methodblock *method) {
  CodeInfo *info;
  ClassClass *clazz;
#if JDK_VER >= 12
  sys_thread_t *self = EE2SysThread(ee);
#endif
  sys_mon_t *mon;

  if ((clazz = fieldclass(&method->fb)) == NULL) {
#ifdef COMPILE_DEBUG
    printf("prepCmpldCodeInfo: methodblock %x is not initialized.\n",
	(int)method);
    fflush(stdout);
#endif
    info = NULL;
    goto prepare_done;
  }


  SYS_MONITOR_ENTER(self, global_monitor);

  info = (CodeInfo *)method->CompiledCodeInfo;
  if (info != NULL) {
    SYS_MONITOR_EXIT(self, global_monitor);
    goto prepare_done;
  }

  info = (CodeInfo *)sysCalloc(1, sizeof(CodeInfo));
  method->CompiledCodeInfo = (void *)info;

  mon = info->monitor = (sys_mon_t *)sysCalloc(1, sysMonitorSizeof());
  sysMonitorInit(info->monitor);

  SYS_MONITOR_EXIT(self, global_monitor);


  SYS_MONITOR_ENTER(self, mon);

  if (info->argsizes != NULL) {
    // already initialized
    SYS_MONITOR_EXIT(self, mon);
    goto prepare_done;
  }

#ifdef EAGER_COMPILATION
  info->cc = NULL;
#endif
#ifdef METHOD_INLINING
  info->inlineability = INLINE_UNKNOWN;
  info->pctablelen = 0;
  info->pctable = NULL;
#endif

#define AS_BUF_SIZE	256
  {
    char *sig = method->fb.signature + 1;
    int i = 0, j = 0;
    char sizes[AS_BUF_SIZE];
    char terse_sig[AS_BUF_SIZE];

    if (!(method->fb.access & ACC_STATIC))
      sizes[i++] = 1;	// receiver

    while (*sig) {
      switch (*sig) {
      case ')':
	sizes[i++] = 0;  terse_sig[j++] = TERSE_SIG_ENDFUNC;  sig++;
	info->ret_sig = sig;
	break;
      case 'V':
	sizes[i++] = 0;  terse_sig[j++] = TERSE_SIG_VOID;  sig++;  break;
      case 'Z':
	sizes[i++] = 1;  terse_sig[j++] = TERSE_SIG_BOOLEAN;  sig++;  break;
      case 'B':
	sizes[i++] = 1;  terse_sig[j++] = TERSE_SIG_BYTE;  sig++;  break;
      case 'S':
	sizes[i++] = 1;  terse_sig[j++] = TERSE_SIG_SHORT;  sig++;  break;
      case 'C':
	sizes[i++] = 1;  terse_sig[j++] = TERSE_SIG_CHAR;  sig++;  break;
      case 'I':
	sizes[i++] = 1;  terse_sig[j++] = TERSE_SIG_INT;  sig++;  break;
      case 'F':
	sizes[i++] = 1;  terse_sig[j++] = TERSE_SIG_FLOAT;  sig++;  break;
      case 'D':
	sizes[i++] = 2;  terse_sig[j++] = TERSE_SIG_DOUBLE;  sig++;  break;
      case 'J':
	sizes[i++] = 2;  terse_sig[j++] = TERSE_SIG_LONG;  sig++;  break;
      case 'L':
	sizes[i++] = 1;  terse_sig[j++] = TERSE_SIG_OBJECT;
	while (*(sig++) != ';');
	break;
      case '[':
	sizes[i++] = 1;  terse_sig[j++] = TERSE_SIG_OBJECT;  sig++;
	while (*sig == '[')  sig++;
	if (*sig++ == 'L')  while (*(sig++) != ';');
	break;
      default:
	/* NOTREACHED */
	fprintf(stderr, "FATAL: invalid signature: %s.\n",
		method->fb.signature);
	JVM_Exit(1);
      }

      if (i >= AS_BUF_SIZE) {
	fprintf(stderr, "FATAL: too many arguments (> %d).\n", AS_BUF_SIZE);
	JVM_Exit(1);
      }
    }	// while (*sig)

    info->argsizes = sysMalloc(i);
    memcpy(info->argsizes, sizes, i);
    info->terse_sig = sysMalloc(j);
    memcpy(info->terse_sig, terse_sig, j);

    info->ret_size = sizes[--i];
  }

#ifdef EXC_BY_SIGNAL
  info->throwtablelen = 0;
#  if 1
  info->throwtablesize = 0;
  info->throwtable = NULL;
#  else
  info->throwtablesize = INITIAL_THROWTABLE_SIZE;
  info->throwtable =
	(throwentry *)sysMalloc(sizeof(throwentry) * INITIAL_THROWTABLE_SIZE);
#  endif
#endif	// EXC_BY_SIGNAL

#ifdef PATCH_WITH_SIGTRAP
  info->trampoline = sysMalloc(43);
  *((uint32_t *)info->trampoline) = 0x00685152;
  info->trampoline[7] = (uint8_t)0x68;
  info->trampoline[12] = (uint8_t)0x68;
  info->trampoline[17] = (uint8_t)0x68;
  info->trampoline[22] = (uint8_t)0xe8;
  info->trampoline[27] = (uint8_t)0x83;
  *((uint32_t *)(info->trampoline + 28)) = 0xc0850cc4;
  *((uint32_t *)(info->trampoline + 32)) = 0x0f5a5958;
  info->trampoline[36] = (uint8_t)0x85;
  *((uint16_t *)(info->trampoline + 41)) = (uint16_t)0xe0ff;

  /*
    pushl  %edx		(0)  52
    pushl  %ecx		(1)  51
    pushl  <orig. PC>	(2)  68 XX XX XX XX
    pushl  cur_cb	(7)  68 XX XX XX XX
    pushl  cb		(12) 68 XX XX XX XX
    pushl  ee		(17) 68 XX XX XX XX
    call   <function>	(22) e8 XX XX XX XX
    addl   $12,%esp	(27) 83 c4 0c
    testl  %eax,%eax	(30) 85 c0
    popl   %eax		(32) 58
    popl   %ecx		(33) 59
    popl   %edx		(34) 5a
    jnz   <exc handler>	(35) 0f 85 XX XX XX XX
    jmp    *%eax	(41) ff e0
  */
#endif	// PATCH_WITH_SIGTRAP


  SYS_MONITOR_EXIT(self, mon);

prepare_done:
  return info;
}


void freeCompiledCodeInfo(CodeInfo *info) {
  if (info) {
    if (info->monitor) {
      sysMonitorDestroy(info->monitor);
      sysFree(info->monitor);
    }
#ifdef EXC_BY_SIGNAL
    if (info->throwtable)  sysFree(info->throwtable);
#endif	// EXC_BY_SIGNAL
    if (info->argsizes)  sysFree(info->argsizes);
    if (info->terse_sig)  sysFree(info->terse_sig);
#ifdef PATCH_WITH_SIGTRAP
    if (info->trampoline)  sysFree(info->trampoline);
#endif	// PATCH_WITH_SIGTRAP
    sysFree(info);
  }
}


#ifdef EXC_BY_SIGNAL
#ifdef CODE_DB
void throwtableExtend(CodeInfo *info, uint32_t size) {
  if (size <= info->throwtablesize)  return;

  if (info->throwtable != NULL) {
    info->throwtable =
	(throwentry *)sysRealloc(info->throwtable, sizeof(throwentry) * size);
  }
  else {
    info->throwtable =
	(throwentry *)sysMalloc(sizeof(throwentry) * size);
  }

  info->throwtablesize = size;
}
#endif	// CODE_DB


throwentry *throwtableAdd(CompilerContext *cc, CodeInfo *info,
	uint32_t start, uint8_t len, uint16_t byteoff) {
  throwentry *entryp;
  sysAssert(info != NULL);

  if (info->throwtablelen >= info->throwtablesize) {	// extend table size
    int size = info->throwtablesize;
    if (size <= 0)
      size = INITIAL_THROWTABLE_SIZE;

    while (size <= info->throwtablelen)
      size <<= 1;

    throwtableExtend(info, size);
  }
#ifdef COMPILE_DEBUG
  if (cc->compile_debug) {
    printf("throwtableAdd(): byte 0x%x, native 0x%x - 0x%x\n",
	byteoff, start, start + len);
    fflush(stdout);
  }
#endif

  entryp = info->throwtable + info->throwtablelen;
  entryp->start = start;
  entryp->byteoff = byteoff;
  entryp->len = len;
#ifdef PATCH_WITH_SIGTRAP
  entryp->patched_code = 0x90;	// nop
  entryp->opcode = 0;
  entryp->cb = NULL;
#endif

  (info->throwtablelen)++;

  return entryp;
}


throwentry *throwtableGet(CodeInfo *info, uint32_t nativeoff) {
  int l = 0;
  int h = info->throwtablelen;
  int index;
  throwentry *entryp;

  while (1) {	// binary search
    index = l + ((h - l) >> 1);

    entryp = info->throwtable + index;
    if ((entryp->start <= nativeoff) &&
	(nativeoff < (entryp->start + entryp->len))) {	// found
      return entryp;
    }

    if (l == h)  return NULL;	// not found

    if (nativeoff < entryp->start)
      h = index;
    else
      l = index + 1;
  }
}
#endif	// EXC_BY_SIGNAL
