/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1998,1999,2000,2001,2002,2003,2004,2005 Kazuyuki Shudo

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

#include <string.h>	// for memcpy(), strlen()
#include <stdlib.h>	// for malloc()
#if defined(__FreeBSD__) || defined(__NetBSD__)
#  include <sys/types.h>	// for a type u_long
#endif
#include <limits.h>	// for INT_MAX
#ifdef _WIN32
#  include <winsock2.h>
#else
#  include <netinet/in.h>	// for ntohl()
#endif

#include "compiler.h"
#include "constants.h"
#include "code.h"	// for macro STSTA
#ifdef METHOD_INLINING
#  include "stack.h"
#endif
#ifdef COUNT_TSC
#  include "x86tsc.h"
#endif


#include <string.h>
#ifdef _WIN32
#  include <limits.h>
// undefine the ntohl macro defined by JDK
#  ifdef ntohl
#    undef ntohl
#  endif
#endif


//
// Global Variables
//
unsigned char *compiledcode_min, *compiledcode_max;


//
// Local Functions
//
static int makePCTable(CompilerContext *cc);
static int processAnOpcode(CompilerContext *cc, int opcode, int byteoff);
static void makeBlockStructure(CompilerContext *cc);

static void updateStates(CompilerContext *cc);
static int writeCode(CompilerContext *cc);
static void resolveJumpInstructions(CompilerContext *cc);
static void resolveExcRetSwitch(CompilerContext *cc);

static int resolveDynamicConstants(CompilerContext *cc);
static void makeExcTable(CompilerContext *cc);
static void resolveFunctionSymbols(CompilerContext *cc);


/*
 * Compile a specified method.
 *
 * returns: true if compile failed.
 */
int compileMethod(struct methodblock *mb, CompilationStage target_stage) {
  CompilerContext *cc;
  CodeInfo *info = (CodeInfo *)mb->CompiledCodeInfo;
#ifdef COUNT_TSC
  int i;
#endif
#ifdef COMPILE_DEBUG
  int compile_debug;
#endif

  // for exclusion
#if JDK_VER >= 12
  sys_thread_t *self;
#endif
  sys_mon_t *mon;


  if ((mb->fb.access & (ACC_ABSTRACT | ACC_NATIVE)) ||
	(mb->invoker == sym_invokeJITCompiledMethod)) {
    // needless to compile
#ifdef COMPILE_DEBUG
    if (compile_debug) {
      printf("  needless to compile: %s#%s %s\n",
		cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
      fflush(stdout);
    }
#endif
    return 0;
  }


  if (mb->code == NULL) {	/* mb->code is NULL */
#ifdef COMPILE_DEBUG
    printf("  mb->code is NULL: %s#%s %s\n",
		cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
    fflush(stdout);
#endif
    return 1;	// failure
  }


  if (info == NULL) {
#ifdef COMPILE_DEBUG
    if (compile_debug) {
      printf("  (cmplMtd(): method->CompiledCodeInfo is NULL)\n");
      fflush(stdout);
    }
#endif
    if ((info = prepareCompiledCodeInfo(EE(), mb)) == NULL)
      return 1;	// failure
  }


  cc = getCompilerContext(mb);

  if (cc->stage >= target_stage)  return 0;

#if JDK_VER >= 12
  self = EE2SysThread(cc->ee);
#endif


#ifdef COMPILE_DEBUG
  compile_debug = cc->compile_debug;
  if (compile_debug) {
    int acc = mb->fb.access;

    printf("\n");
    printf("cmplMtd() called.\n  %s#%s %s\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
    printf("  cur stage: %d, target stage: %d\n", cc->stage, target_stage);

    showCompilerContext(cc, "  ");

    printf("  access: 0x%x", acc);
    if (acc & ACC_NATIVE)  printf(" native");
//    if (acc & ACC_MACHINE_COMPILED)  printf(" machine_compiled");
    if (acc & ACC_PUBLIC)  printf(" public");
    if (acc & ACC_PRIVATE)  printf(" private");
    if (acc & ACC_PROTECTED)  printf(" protected");
    if (acc & ACC_STATIC)  printf(" static");
    if (acc & ACC_FINAL)  printf(" final");
    if (acc & ACC_SYNCHRONIZED)  printf(" synchronized");
    if (acc & ACC_ABSTRACT)  printf(" abstract");
    if (acc & ACC_STRICT)  printf(" strictfp");
    printf("\n");

    printf("  nlocals:  %d\n", mb->nlocals);
    printf("  maxstack: %d\n", mb->maxstack);

#  ifdef METAVM
    printf("  remote flag: 0x%x (%x)\n", GET_REMOTE_FLAG(cc->ee) & 0xff, EE());
    printf("  remote addr: 0x%x\n", REMOTE_ADDR(cc->ee));
    printf("  excKind: %d\n", cc->ee->exceptionKind);
#  endif

    fflush(stdout);
  }
#endif	// COMPILE_DEBUG


  mon = info->monitor;
#ifdef COMPILE_DEBUG
  if (compile_debug) {
    printf("  monitor: 0x%x\n", mon);
#if JDK_VER >= 12
    printf("  thread:  0x%x\n", self);
#endif
    fflush(stdout);
  }
#endif
  SYS_MONITOR_ENTER(self, mon);		// lock


  if (cc->stage == STAGE_DONE)
    goto stage_done;

  mb->invoker = access2invoker(mb->fb.access);

  // jump into an intermediate stage
  if (cc->stage == STAGE_INTERNAL_CODE)
    goto stage_internal_code;
  else if (cc->stage == STAGE_STATIC_PART)
    goto stage_static_part;

#ifdef CODE_DB
  else if (OPT_SETQ(OPT_CODEDB)) {
    if (readCompiledCode(db, db_page, cc)) {
      cc->stage = STAGE_STATIC_PART;
      goto stage_static_part;
    }
  }	// if (OPT_CODEDB)
#endif


  // compile
#ifdef COUNT_TSC
#  define CALL_RDTSC(N)	cc->tsc[N] = rdtsc()
#else
#  define CALL_RDTSC(N)
#endif

  CALL_RDTSC(0);
  CALL_RDTSC(0);
  if (makePCTable(cc))  goto compile_failed;
  CALL_RDTSC(1);
  makeBlockStructure(cc);
  CALL_RDTSC(2);
#ifdef OPTIMIZE_INTERNAL_CODE
  peepholeOptimization(cc);
#endif

#ifdef METHOD_INLINING
  if (!(mb->fb.access & ACC_SYNCHRONIZED) &&	// not synchronized
      (mb->exception_table_length == 0) &&	// don't have exc. table
      (cc->may_jump == FALSE) &&		// don't contain any jump
      (pctableLen(cc) <= opt_inlining_maxlen)) {
    size_t copysize = sizeof(pcentry) * pctableLen(cc);

    info->inlineability = INLINE_MAY;

    // preserve generated internal instructions  for inlining
    info->pctable = sysMalloc(copysize);
    memcpy(info->pctable, cc->pctable, copysize);
    info->pctablelen = cc->pctablelen;
  }
  else
    info->inlineability = INLINE_DONT;
#endif

  cc->stage = STAGE_INTERNAL_CODE;

stage_internal_code:
  if (cc->stage >= target_stage) {
    mb->invoker = sym_compileAndInvokeMethod;
    goto stage_done;
  }

  CALL_RDTSC(3);
  methodInlining(cc);
  CALL_RDTSC(4);
#ifdef EAGER_COMPILATION
  eagerCompilation(cc);
#endif
  CALL_RDTSC(5);
  updateStates(cc);
  CALL_RDTSC(6);
  if (writeCode(cc))  goto compile_failed;
  CALL_RDTSC(7);
  resolveJumpInstructions(cc);
  CALL_RDTSC(8);
  resolveExcRetSwitch(cc);
  CALL_RDTSC(9);

  // set generated code to methodblock
  {
    int code_size = cc->bufp - cc->buffer;
#ifdef COMPILE_DEBUG
    if (compile_debug) {
      printf("cmplMtd(): generated code size = 0x%x(%d)\n",
	code_size, code_size);
    }
#endif
    info->code_size = code_size;

    {
      unsigned char *code = (void *)sysMalloc(code_size);
      memcpy(code, cc->buffer, code_size);
      mb->CompiledCode = code;

      if (code < compiledcode_min)
        compiledcode_min = code;
      if (compiledcode_max < (code + code_size))
        compiledcode_max = code + code_size;
    }
#ifdef COMPILE_DEBUG
    if (compile_debug) {
      printf("  mb->CompiledCode: 0x%08x\n", (int)mb->CompiledCode);
      fflush(stdout);
    }
#endif
  }

#ifdef CODE_DB
  if (OPT_SETQ(OPT_CODEDB))
    writeCompiledCode(db, db_page, cc);
#endif	// CODE_DB

  cc->stage = STAGE_STATIC_PART;

stage_static_part:
  if (cc->stage >= target_stage) {
    mb->invoker = sym_compileAndInvokeMethod;
    goto stage_done;
  }

  CALL_RDTSC(10);
  resolveDynamicConstants(cc);
  CALL_RDTSC(11);
  makeExcTable(cc);
  CALL_RDTSC(12);
  resolveFunctionSymbols(cc);
  CALL_RDTSC(13);

#ifdef DIRECT_INVOCATION
  mb->fb.access |= ACC_MACHINE_COMPILED;
#endif

  mb->invoker =
	(bool_t (*)(JHandle *, struct methodblock *, int, struct execenv *))
		sym_invokeJITCompiledMethod;
			// invokeJITCompiledMethod() is in invoker.c
#ifdef COUNT_TSC
  printf("%s#%s %s\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
  {
    unsigned long long int diff, all;

    all = cc->tsc[N_TSC] - cc->tsc[0];
    printf("  all    : %7llu\n", all);

    for (i = 1; i <= N_TSC; i++) {
      diff = cc->tsc[i] - cc->tsc[i - 1];
      printf("  tsc[%2d]: %7llu   %4.1f \%\n", i, diff,
	(double)diff * 100.0 / all);
    }
  }
#endif

  // write code size

  if (OPT_SETQ(OPT_CODESIZE)) {
    static FILE *sizefp = NULL;

    if (!sizefp) {
      if (!(sizefp = fopen(CODESIZE_FNAME, "w"))) {
	perror("fopen");
	OPT_RESET(OPT_CODESIZE);
	goto codesize_open_failed;
      }

      fprintf(sizefp, "# num_bytecode"
		" size_bytecode"
		" size_native_code"
#ifdef EXC_BY_SIGNAL
		" num_throw_entry"
		" size_throw_entry"
#endif
		" class_name#method_name signature\n");
    }

    fprintf(sizefp, "%d", cc->ninsn);
    fprintf(sizefp, "\t%lu", mb->code_length);
    fprintf(sizefp, "\t%d", info->code_size);
#ifdef EXC_BY_SIGNAL
    fprintf(sizefp, "\t%d", info->throwtablelen);
    fprintf(sizefp, "\t%d", info->throwtablelen * sizeof(throwentry));
#endif
    fprintf(sizefp, "\t%s#%s %s",
		cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
	// class name, method name, signature
    fprintf(sizefp, "\n");
    fflush(sizefp);
  }
codesize_open_failed:


  // write generated code to the file code_<classname><methodname>.c

  if (OPT_SETQ(OPT_OUTCODE)) {
    FILE *codefp;
    char funcname[256];
    char fname[256];
    char *p;
    unsigned char *u;
    int i;

    snprintf(funcname, 256, "%s_%s%s",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
    for (p = funcname; *p != '\0'; p++) {
      switch (*p) {
      case '(':
      case ')':
	*p = '_';  break;
      case '/':
      case ';':  case '<':  case '>':
	*p = '_';  break;
      case '[':  *p = '_';  break;
      }
    }

    snprintf(fname, 256, "code-%s.S", funcname);

    if (!(codefp = fopen(fname, "w"))) {
      perror("fopen");  goto code_open_failed;
    }

#if defined(_WIN32) || ((defined(__FreeBSD__) || defined(__NetBSD__)) && !defined(__ELF__))
    fprintf(codefp, ".globl _%s\n", funcname);
#else
    fprintf(codefp, ".globl %s\n", funcname);
#endif
#ifdef linux
    fprintf(codefp, ".align 16,0x90\n");
#endif
    fprintf(codefp, "%s:", funcname);
    fprintf(codefp, ".byte ");
    u = (unsigned char *)mb->CompiledCode;
    fprintf(codefp, "%d", u[0]);
    for (i = 1; i < info->code_size; i++)  fprintf(codefp, ",%d", u[i]);
    fprintf(codefp, "\n");

    fclose(codefp);
  }
code_open_failed:


  cc->stage = STAGE_DONE;

  releaseCompilerContext(cc);

stage_done:
  SYS_MONITOR_EXIT(self, mon);		// unlock

#ifdef COMPILE_DEBUG
  if (compile_debug) {
    printf("cmplMtd() done.\n  %s#%s %s\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
    printf("  code: 0x%08x, size: 0x%x(%d)\n",
	(int)mb->CompiledCode, info->code_size, info->code_size);
    fflush(stdout);
  }
#endif

  return 0;


compile_failed:
  SYS_MONITOR_EXIT(self, mon);		// unlock

#ifdef COMPILE_DEBUG
  printf("compileMethod() failed.\n  %s#%s %s\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
  fflush(stdout);
#endif

#ifdef DIRECT_INVOCATION
  mb->fb.access &= ~ACC_MACHINE_COMPILED;
#endif
  // mb->invoker = access2invoker(mb->fb.access);	/* needless */

  releaseCompilerContext(cc);

  return 1;
}


/*
 * Freeing method related stuffs.
 */
void freeMethod(struct methodblock *mb) {
  void *code;
#ifdef COMPILE_DEBUG
  printf("freeMethod(): ");
  if (mb)
    printf("%s#%s %s\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
  else
    printf("(null)\n");
  fflush(stdout);
#endif

  freeCompiledCodeInfo(mb->CompiledCodeInfo);
  mb->CompiledCodeInfo = NULL;

  code = mb->CompiledCode;
  if (code)  sysFree(code);
  mb->CompiledCode = NULL;
}


/*
 * 1st pass of compilation.
 * generate native code from byte code.
 *
 * returns: true if an exception occurred.
 */
static int makePCTable(CompilerContext *cc) {
  struct methodblock *mb = cc->mb;
  unsigned char *methodcode = mb->code;
  int endoff = mb->code_length;
  int32_t byteoff = 0;
  int byteinc;
  int operand;
  cp_item_type *constant_pool = cbConstantPool(fieldclass(&mb->fb));
  unsigned char *type_table =
	constant_pool[CONSTANT_POOL_TYPE_TABLE_INDEX].type;

  CodeInfo *info = (CodeInfo *)(mb->CompiledCodeInfo);
#if 0
  int32_t invocation_count = info->invocation_count;
#endif
  int remaked = 0;	// makePCTable() is retried once at least

  int opcode;
#ifdef COMPILE_DEBUG
  int compile_debug = cc->compile_debug;
#endif

makepctable_start:
  processAnOpcode(cc, opc_methodhead, -1);

#ifdef METAVM
  if (!strcmp(cbName(fieldclass(&mb->fb)), JAVAPKG "Thread") &&
      !strcmp(mb->fb.name, "run")) {
#ifdef COMPILE_DEBUG
    printf("generate `metavm_init' for Thread#run.\n");  fflush(stdout);
#endif
    processAnOpcode(cc, opc_metavm_init, -1);
  }
#endif	// METAVM

#ifndef USE_SSE2
  if (((mb->fb.access & ACC_STRICT) && !OPT_SETQ(OPT_IGNSTRICTFP)) ||
      OPT_SETQ(OPT_FRCSTRICTFP)) {
    if (!is_fpupc_double) {
      processAnOpcode(cc, opc_fppc_save, -1);
      processAnOpcode(cc, opc_fppc_double, -1);
    }

    processAnOpcode(cc, opc_strict_enter, -1);
  }
#endif	// !USE_SSE2


  if ((mb->fb.access & ACC_SYNCHRONIZED) && !OPT_SETQ(OPT_IGNLOCK)) {
    if (mb->fb.access & ACC_STATIC)
      processAnOpcode(cc, opc_sync_static_enter, -1);
    else
      processAnOpcode(cc, opc_sync_obj_enter, -1);
  }


  processAnOpcode(cc, opc_start, -1);


  while (byteoff < endoff) {
    cc->ninsn++;
    opcode = *(methodcode + byteoff);

    // decompose an opcode to some micro opcodes
    switch (opcode) {
    case opc_iaload:  case opc_laload:  case opc_faload:  case opc_daload:
    case opc_aaload:  case opc_baload:  case opc_caload:  case opc_saload:
      processAnOpcode(cc, opc_fill_cache, byteoff);
      processAnOpcode(cc, opc_array_check, byteoff);
      byteoff += processAnOpcode(cc, opcode, byteoff);
      break;

    case opc_lstore:  case opc_dstore:
    case opc_lstore_0:  case opc_dstore_0:
    case opc_lstore_1:  case opc_dstore_1:
    case opc_lstore_2:  case opc_dstore_2:
    case opc_lstore_3:  case opc_dstore_3:
      processAnOpcode(cc, opc_fill_cache, byteoff);
      byteoff += processAnOpcode(cc, opcode, byteoff);
      break;

    case opc_iastore:  case opc_fastore:
    case opc_aastore:  case opc_bastore:  case opc_castore:  case opc_sastore:
      processAnOpcode(cc, opc_iastore1, byteoff);
      processAnOpcode(cc, opc_fill_cache, byteoff);
      processAnOpcode(cc, opc_array_check, byteoff);
      byteoff += processAnOpcode(cc, opcode, byteoff);
      break;

    case opc_lastore:  case opc_dastore:
      processAnOpcode(cc, opc_fill_cache, byteoff);
      byteoff += processAnOpcode(cc, opcode, byteoff);
      break;

    case opc_fadd:  case opc_fsub:
      {
	int opcode_fld = ((opcode == opc_fadd) ? opc_fld : opc_fld4);

	processAnOpcode(cc, opc_flush_cache, byteoff);
	processAnOpcode(cc, opcode_fld, byteoff);
	byteinc = processAnOpcode(cc, opcode, byteoff);
	processAnOpcode(cc, opc_fst, byteoff);
	byteoff += byteinc;
      }
      break;
    case opc_fmul:  case opc_fdiv:
      {
	int opcode_fld = ((opcode == opc_fmul) ? opc_fld : opc_fld4);

#if !defined(USE_SSE2) || !defined(OMIT_SCALING_SINGLE_PRECISION)
	if (OPT_SETQ(OPT_FRCSTRICTFP) ||
		(!OPT_SETQ(OPT_IGNSTRICTFP) && (mb->fb.access & ACC_STRICT))) {
	  processAnOpcode(cc, opc_flush_cache, byteoff);
	  processAnOpcode(cc, opc_strict_fprep, byteoff);
	  processAnOpcode(cc, opcode_fld, byteoff);
	  processAnOpcode(cc, opc_strict_fscdown, byteoff);
	  byteinc = processAnOpcode(cc, opcode, byteoff);
	  processAnOpcode(cc, opc_strict_fscup, byteoff);
	  processAnOpcode(cc, opc_fst, byteoff);
	  processAnOpcode(cc, opc_strict_fsettle, byteoff);
	}
	else
#endif	// !USE_SSE2
	{
	  processAnOpcode(cc, opc_flush_cache, byteoff);
	  processAnOpcode(cc, opcode_fld, byteoff);
	  byteinc = processAnOpcode(cc, opcode, byteoff);
	  processAnOpcode(cc, opc_fst, byteoff);
	}
	byteoff += byteinc;
      }
      break;
    case opc_dadd:  case opc_dsub:
      {
	int opcode_dld = ((opcode == opc_dadd) ? opc_dld : opc_dld8);

	processAnOpcode(cc, opc_flush_cache, byteoff);
	processAnOpcode(cc, opcode_dld, byteoff);
	byteinc = processAnOpcode(cc, opcode, byteoff);
	processAnOpcode(cc, opc_dst, byteoff);
	byteoff += byteinc;
      }
      break;
    case opc_dmul:  case opc_ddiv:
      {
	int opcode_dld = ((opcode == opc_dmul) ? opc_dld : opc_dld8);

#ifndef USE_SSE2
	if (OPT_SETQ(OPT_FRCSTRICTFP) ||
		(!OPT_SETQ(OPT_IGNSTRICTFP) && (mb->fb.access & ACC_STRICT))) {
	  processAnOpcode(cc, opc_flush_cache, byteoff);
	  processAnOpcode(cc, opc_strict_dprep, byteoff);
	  processAnOpcode(cc, opcode_dld, byteoff);
	  processAnOpcode(cc, opc_strict_dscdown, byteoff);
	  byteinc = processAnOpcode(cc, opcode, byteoff);
	  processAnOpcode(cc, opc_strict_dscup, byteoff);
	  processAnOpcode(cc, opc_dst, byteoff);
	  processAnOpcode(cc, opc_strict_dsettle, byteoff);
	}
	else
#endif	// !USE_SSE2
	{
	  processAnOpcode(cc, opc_flush_cache, byteoff);
	  processAnOpcode(cc, opcode_dld, byteoff);
	  byteinc = processAnOpcode(cc, opcode, byteoff);
	  processAnOpcode(cc, opc_dst, byteoff);
	}
	byteoff += byteinc;
      }
      break;

    case opc_i2f:
#ifndef USE_SSE2
    case opc_frem:
    case opc_l2f:
#endif
      processAnOpcode(cc, opc_flush_cache, byteoff);
      byteinc = processAnOpcode(cc, opcode, byteoff);
      processAnOpcode(cc, opc_fst, byteoff);
      byteoff += byteinc;
      break;

    case opc_drem:
      processAnOpcode(cc, opc_fill_cache, byteoff);
      byteinc = processAnOpcode(cc, opcode, byteoff);
#ifndef USE_SSE2
      processAnOpcode(cc, opc_dst, byteoff);
#endif
      byteoff += byteinc;
      break;

    case opc_i2d:
#ifndef USE_SSE2
    case opc_l2d:
#endif
      processAnOpcode(cc, opc_flush_cache, byteoff);
      byteinc = processAnOpcode(cc, opcode, byteoff);
      processAnOpcode(cc, opc_dst, byteoff);
      byteoff += byteinc;
      break;

#ifdef USE_SSE2
    case opc_frem:
    case opc_l2f:
    case opc_l2d:
      processAnOpcode(cc, opc_flush_cache, byteoff);
      byteinc = processAnOpcode(cc, opcode, byteoff);
      byteoff += byteinc;
      break;
#endif

    case opc_f2d:
      processAnOpcode(cc, opc_flush_cache, byteoff);
      processAnOpcode(cc, opc_fld, byteoff);
      byteinc = processAnOpcode(cc, opcode, byteoff);
      processAnOpcode(cc, opc_dst, byteoff);
      byteoff += byteinc;
      break;

    case opc_d2f:
      processAnOpcode(cc, opc_flush_cache, byteoff);
      processAnOpcode(cc, opc_dld, byteoff);
      byteinc = processAnOpcode(cc, opcode, byteoff);
      processAnOpcode(cc, opc_fst, byteoff);
      byteoff += byteinc;
      break;

    case opc_ireturn:  case opc_freturn:  case opc_areturn:
      processAnOpcode(cc, opc_stateto1, byteoff);
      processAnOpcode(cc, opc_return, byteoff);
      byteoff++;
      break;
      
    case opc_lreturn:  case opc_dreturn:
      processAnOpcode(cc, opc_stateto4, byteoff);
      processAnOpcode(cc, opc_return, byteoff);
      byteoff++;
      break;

    case opc_invokevirtual:  case opc_invokevirtual_quick_w:
    case opc_invokespecial:
    case opc_invokenonvirtual_quick:  case opc_invokesuper_quick:
    case opc_invokestatic:  case opc_invokestatic_quick:
    case opc_invokeinterface:  case opc_invokeinterface_quick:
      switch (opcode) {
      case opc_invokevirtual:  case opc_invokevirtual_quick_w:
	opcode = opc_invokevirtual;
	break;
      case opc_invokespecial:
      case opc_invokenonvirtual_quick:  case opc_invokesuper_quick:
	opcode = opc_invokespecial;
	break;
      case opc_invokestatic:  case opc_invokestatic_quick:
	opcode = opc_invokestatic;
	break;
      case opc_invokeinterface:  case opc_invokeinterface_quick:
	opcode = opc_invokeinterface;
      }

      operand = GET_UINT16(methodcode + byteoff + 1);
      byteinc = opcode_length[opcode];

      // throwing NoClassDefFoundError if the callee method cannot be resolved
      if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {
	if (!ResolveClassConstantFromClass2(fieldclass(&mb->fb), operand,
		cc->ee, (1 << CONSTANT_Methodref) |
			(1 << CONSTANT_InterfaceMethodref), FALSE)) {
	  JHandle *exc;
#ifdef COMPILE_DEBUG
	  printf("resolution failed (cp index: %d).\n", operand);
	  fflush(stdout);
#endif
	  sysAssert(exceptionOccurred(cc->ee));
	  exc = cc->ee->exception.exc;
	  sysAssert(exc != NULL);
	  if (obj_classblock(exc) == classJavaLangNoClassDefFoundError) {
	    exceptionClear(cc->ee);
	    processAnOpcode(cc, opc_throw_noclassdef, byteoff);
	    goto makepc_invoke_done;
	  }
	  else if (obj_classblock(exc) == classJavaLangNoSuchMethodError) {
	    exceptionClear(cc->ee);
	    processAnOpcode(cc, opc_throw_nomethod, byteoff);
	    goto makepc_invoke_done;
	  }

	  return 1;
	}
      }

      {
	struct methodblock *src_mb;
	struct fieldblock *method_fb;
	ClassClass *src_clazz, *method_clazz;

	// check IllegalAccessError
	if ((src_mb = cc->mb) == NULL) {
#ifdef COMPILE_DEBUG
	  printf("Illegal method invocation: methodblock is null.\n");
	  fflush(stdout);
#endif
	  processAnOpcode(cc, opc_throw_illegalaccess, byteoff);
	  goto makepc_invoke_done;
	}
	method_fb = &constant_pool[operand].mb->fb;

	src_clazz = fieldclass(&src_mb->fb);
	method_clazz = fieldclass(method_fb);

#ifndef SLACK_ACCESS_CONTROL
	// check private and protected from other packages
	if (!src_mb ||
	    (!VerifyFieldAccess(
		src_clazz, method_clazz,
		method_fb->access, FALSE))) {
	  char *src_clazz_name, *method_clazz_name;
	  char *src_p, *method_p;
	  int len = 0;

	  src_clazz_name = cbName(src_clazz);
	  method_clazz_name = cbName(method_clazz);

#ifdef COMPILE_DEBUG
	  printf("Illegal method invocation:\n");  fflush(stdout);
	  printf("\tfrom: %s#%s\n",
		src_clazz_name, src_mb->fb.name);
	  printf("\tto  : %s#%s\n",
		method_clazz_name, method_fb->name);
#endif

	  // but, allow access to/from inner class
	  src_p = strchr(src_clazz_name, '$');
	  method_p = strchr(method_clazz_name, '$');
	  if (src_p) {
	    if (!method_p)  len = src_p - src_clazz_name;
	  }
	  else {
	    if (method_p)  len = method_p - method_clazz_name;
	  }
	  if (len &&
#if JDK_VER >= 12
	(cbProtectionDomain(src_clazz) == cbProtectionDomain(method_clazz)) &&
	(cbLoader(src_clazz) == cbLoader(method_clazz))
#else
	      TRUE
#endif	// JDK_VER >= 12
	      ) {
	    if (!strncmp(src_clazz_name, method_clazz_name, len)) {
#ifdef COMPILE_DEBUG
	      printf("  VerifyFieldAccess returned FALSE, "
			"but the access is to/from inner class.\n");
	      fflush(stdout);
#endif
	      goto check_illacc_done;
	    }
	  }

#ifdef COMPILE_DEBUG
	  printf("  A private or protected method invoked.\n");
	  fflush(stdout);
#endif
	  processAnOpcode(cc, opc_throw_illegalaccess, byteoff);
	  goto makepc_invoke_done;
	}
#endif	// SLACK_ACCESS_CONTROL
      }
    check_illacc_done:

      switch (opcode) {
      case opc_invokevirtual:
	{
	  struct methodblock *method = constant_pool[operand].mb;
	  ClassClass *clz = fieldclass(&method->fb);

	  if ((method->fb.access & ACC_PRIVATE) ||
	      ((method->fb.access | cbAccess(clz)) & ACC_FINAL)) {
	    // the method is private and the method or the class is final
#ifdef COMPILE_DEBUG
	    if (compile_debug) {
	      printf("  adjust invokevirtual private method to "
			"invokespecial.\n");
	      fflush(stdout);
	    }
#endif
	    opcode = opc_invokespecial;
	    goto makepc_invokespecial;
	  }

	  processAnOpcode(cc, opc_stateto0, byteoff);
	  processAnOpcode(cc, opc_inv_head, byteoff);
	  processAnOpcode(cc, opc_inv_vir_obj, byteoff);
	  if (!strcmp(cbName(clz), "java/lang/Object"))
	    processAnOpcode(cc, opc_invokevirtual, byteoff);
	  else
	    processAnOpcode(cc, opc_invokevirtual_obj, byteoff);
#ifdef METAVM
	  processAnOpcode(cc, opc_inv_metavm, byteoff);
#endif
	  processAnOpcode(cc, opc_inv_vir_varspace, byteoff);
	  processAnOpcode(cc, opc_invoke_core, byteoff);
	}
	break;

      case opc_invokespecial:
      case opc_invokestatic:
      makepc_invokespecial:
	// translate invokevirtual Object#<init> to invokeignored_quick
	{
	  struct methodblock *method = constant_pool[operand].mb;

	  if (!(method->fb.access & (ACC_NATIVE | ACC_ABSTRACT)) &&
	      (method->code[0] == opc_return)) {
	    unsigned char *bytepc = methodcode + byteoff;
	    int toNullCheck = ((method->fb.access & ACC_STATIC) == 0)
#if JDK_VER >= 12
			&& (strcmp(method->fb.name, "<init>") != 0)
#endif
			;
		// condition: !static && !constructor
		// omit null checks for constructors if JDK is 1.2 or later
#ifdef NO_NULL_AND_ARRAY_CHECK
	    processAnOpcode(cc, opc_invokeignored_static_quick, byteoff);
#else
	    if (toNullCheck) {
	      processAnOpcode(cc, opc_invokeignored_quick, byteoff);
	    }
	    else {
	      opcode = ((CB_INITIALIZED(fieldclass(&method->fb))) ?
		opc_invokeignored_static_quick : opc_invokeignored_static);
	      processAnOpcode(cc, opcode, byteoff);
	    }
#endif	// NO_NULL_AND_ARRAY_CHECK

	    // rewrite bytecode instruction
#if JDK_VER >= 12
	    CODE_LOCK(EE2SysThread(cc->ee));
#else
	    BINCLASS_LOCK();
#endif	// JDK_VER
	    bytepc[0] = opc_invokeignored_quick;
	    bytepc[1] = method->args_size;
	    bytepc[2] = (unsigned char)toNullCheck;
		// indicates whether to be checked or not
#if JDK_VER >= 12
	    CODE_UNLOCK(EE2SysThread(cc->ee));
#else
	    BINCLASS_UNLOCK();
#endif	// JDK_VER

	    goto makepc_invoke_done;
	  }
	}

#ifdef ELIMINATE_TAIL_RECURSION
	{
	  struct methodblock *method = constant_pool[operand].mb;
	  if ((cc->mb /* not mb */ == method) &&	// recursive call
	      (!(method->fb.access & ACC_SYNCHRONIZED))) {  // not synchronized
	    int next_bytepc = byteoff + byteinc;
	    int next_opcode = *(methodcode + next_bytepc);
	    while ((next_opcode == opc_goto) || (next_opcode == opc_goto_w)) {
	      if (next_opcode == opc_goto) {
		next_bytepc += GET_INT16(methodcode + next_bytepc + 1);
	      }
	      else {
		next_bytepc += GET_INT32(methodcode + next_bytepc + 1);
	      }
	      next_opcode = *(methodcode + next_bytepc);
	    }

	    if ((opc_ireturn <= next_opcode) && (next_opcode <= opc_return)) {
	      // next insn. is return
	      if (searchCatchFrame(cc->ee, cc->mb, byteoff
#  ifdef RUNTIME_DEBUG
				, COMPILE_DEBUG
#  endif
				) == NULL) {
		// the call is not in a try clause

		// tail recursion
		int reccall_opcode;
#  ifdef COMPILE_DEBUG
		if (compile_debug) {
		  printf("tail recursion.\n");
		  printf("  args_size: %d\n", method->args_size);
		  fflush(stdout);
		}
#  endif
		switch (method->args_size) {
		case 1:
		  reccall_opcode = opc_invoke_recursive_1;  break;
		case 2:
		  reccall_opcode = opc_invoke_recursive_2;  break;
		case 3:
		  reccall_opcode = opc_invoke_recursive_3;  break;
		default:
		  reccall_opcode = opc_invoke_recursive;  break;
		}

		processAnOpcode(cc, opc_stateto0, byteoff);
		processAnOpcode(cc, reccall_opcode, byteoff);

		goto makepc_invoke_done;
	      }	// if (searchCatchFrame(...))
#  ifdef COMPILE_DEBUG
	      else {
		if (compile_debug) {
		  printf("not tail recursion:\n"
			"  calling insn is in a catch frame.\n");
		  fflush(stdout);
		}
	      }
#  endif
	    }	// if (opc_ireturn <= next_opcode <= opc_return)
	  }
	}
#endif	// ELIMINATE_TAIL_RECURSION

	switch (opcode) {
	case opc_invokespecial:
	  {
#ifdef SPECIAL_INLINING
#  if JDK_VER >= 12
	    // special inlining
	    bool_t inlined = FALSE;
	    struct methodblock *method = constant_pool[operand].mb;

	    if (!strcmp(cbName(fieldclass(&method->fb)),
			"java/io/BufferedInputStream") &&
		!strcmp(method->fb.name, "ensureOpen")) {
	      inlined = TRUE;
	      processAnOpcode(cc, opc_java_io_bufferedinputstream_ensureopen,
			byteoff);
	    }
	    if (!inlined)
#  endif	// JDK_VER >= 12
#endif	// SPECIAL_INLINING
	    {
	      processAnOpcode(cc, opc_stateto0, byteoff);
	      processAnOpcode(cc, opc_inv_head, byteoff);
	      processAnOpcode(cc, opc_inv_spe_obj, byteoff);
	      processAnOpcode(cc, opc_invokespecial, byteoff);
#ifdef METAVM
	      processAnOpcode(cc, opc_inv_metavm, byteoff);
#endif
	      processAnOpcode(cc, opc_inv_spe_varspace, byteoff);
	      processAnOpcode(cc, opc_invoke_core, byteoff);
	    }
	  }
	  break;

	case opc_invokestatic:
	  {
#ifdef SPECIAL_INLINING
	    // special inlining
	    bool_t inlined = FALSE;
	    struct methodblock *method = constant_pool[operand].mb;
	    char *cname = cbName(fieldclass(&method->fb));

	    if (!strcmp(cname, "java/lang/Math")) {
	      char *mname = method->fb.name;
	      char *sig = method->fb.signature;
#  ifdef COMPILE_DEBUG
	      if (compile_debug) {
		printf("  invocation of Math#%s.\n", mname);
	      }
#  endif
	      if (!strcmp(mname, "sqrt")) {
		inlined = TRUE;  processAnOpcode(cc, opc_sqrt, byteoff);
	      }
	      else if (!strcmp(mname, "sin")) {
		inlined = TRUE;  processAnOpcode(cc, opc_sin, byteoff);
	      }
	      else if (!strcmp(mname, "cos")) {
		inlined = TRUE;  processAnOpcode(cc, opc_cos, byteoff);
	      }
	      else if (!strcmp(mname, "tan")) {
		inlined = TRUE;  processAnOpcode(cc, opc_tan, byteoff);
	      }
	      else if (!strcmp(mname, "atan2")) {
		inlined = TRUE;  processAnOpcode(cc, opc_atan2, byteoff);
	      }
	      else if (!strcmp(mname, "atan")) {
		inlined = TRUE;  processAnOpcode(cc, opc_atan, byteoff);
	      }
	      else if (!strcmp(mname, "log")) {
		inlined = TRUE;  processAnOpcode(cc, opc_log, byteoff);
	      }
	      else if (!strcmp(mname, "floor")) {
		inlined = TRUE;  processAnOpcode(cc, opc_floor, byteoff);
	      }
	      else if (!strcmp(mname, "ceil")) {
		inlined = TRUE;  processAnOpcode(cc, opc_ceil, byteoff);
	      }
#  if JDK_VER >= 12
	      else if (!strcmp(mname, "exp")) {
		// behavior of pre-assembled code differs from an interpreter.
		inlined = TRUE;  processAnOpcode(cc, opc_exp, byteoff);
	      }
	      else if (!strcmp(mname, "asin")) {
		// behavior of pre-assembled code differs from an interpreter.
		inlined = TRUE;  processAnOpcode(cc, opc_asin, byteoff);
	      }
	      else if (!strcmp(mname, "acos")) {
		// behavior of pre-assembled code differs from an interpreter.
		inlined = TRUE;  processAnOpcode(cc, opc_acos, byteoff);
	      }
#  endif	// JDK_VER >= 12
	    }	// if "java/lang/Math"

	    if (!strcmp(cname, "java/lang/Math") ||
		!strcmp(cname, "java/lang/StrictMath")) {
	      char *mname = method->fb.name;
	      char *sig = method->fb.signature;
#  ifdef COMPILE_DEBUG
	      if (compile_debug) {
		printf("  invocation of Math or StrictMath#%s.\n", mname);
	      }
#  endif
	      if (!strcmp(mname, "abs")) {
		if (!strcmp(sig, "(I)I")) {
		  inlined = TRUE;
		  processAnOpcode(cc, opc_abs_int, byteoff);
		}
		else if (!strcmp(sig, "(J)J")) {
		  inlined = TRUE;
		  processAnOpcode(cc, opc_abs_long, byteoff);
		}
		else if (!strcmp(sig, "(F)F")) {
		  inlined = TRUE;
		  processAnOpcode(cc, opc_abs_float, byteoff);
		}
		else if (!strcmp(sig, "(D)D")) {
		  inlined = TRUE;
		  processAnOpcode(cc, opc_abs_double, byteoff);
		}
	      }
	    }	// if "java/lang/Math" or "java/lang/StrictMath"

	    if (!inlined)
#endif	// SPECIAL_INLINING
	    {
	      processAnOpcode(cc, opc_stateto0, byteoff);
	      processAnOpcode(cc, opc_inv_head, byteoff);
	      processAnOpcode(cc, opc_inv_stq_obj, byteoff);
	      if (
#ifdef CODEDB
		  OPT_SETQ(OPT_CODEDB) ||
#endif
		 !CB_INITIALIZED(fieldclass(&(constant_pool[operand].mb->fb))))
		processAnOpcode(cc, opc_invokestatic, byteoff);
	      else
		processAnOpcode(cc, opc_invokestatic_quick, byteoff);
	      processAnOpcode(cc, opc_inv_stq_varspace, byteoff);
	      processAnOpcode(cc, opc_invoke_core, byteoff);
	    }
	  }
	}
	break;

      case opc_invokeinterface:
	processAnOpcode(cc, opc_stateto0, byteoff);
	processAnOpcode(cc, opc_inv_head, byteoff);
	processAnOpcode(cc, opc_inv_spe_obj, byteoff);
	processAnOpcode(cc, opc_invokeinterface, byteoff);
#ifdef METAVM
	processAnOpcode(cc, opc_inv_metavm, byteoff);
#endif
	processAnOpcode(cc, opc_inv_vir_varspace, byteoff);
	processAnOpcode(cc, opc_invoke_core, byteoff);
	break;
      }	// switch (opcode)

    makepc_invoke_done:
      byteoff += byteinc;
      break;

    case opc_invokevirtualobject_quick:
    case opc_invokevirtual_quick:
      fprintf(stderr, "FATAL: lossy quick opcodes found: %d(0x%x)\n",
		opcode, opcode);
      JVM_Exit(1);
      break;

    case opc_monitorenter:  case opc_monitorexit:
      if (OPT_SETQ(OPT_IGNLOCK))  break;
      // go through

    default:
      byteinc = processAnOpcode(cc, opcode, byteoff);
      if (byteinc < 0)  return 1;
      byteoff += byteinc;
      break;
    }
  }


  processAnOpcode(cc, opc_end, INT_MAX);

  if ((mb->exception_table_length > 0) || (cc->may_throw)) {
    processAnOpcode(cc, opc_exc_handler, INT_MAX);
  }
#ifdef COMPILE_DEBUG
  else {
    if (compile_debug) {
      printf("exc_handler omitted: %s#%s %s\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
      fflush(stdout);
    }
  }
#endif

  // method tail
  processAnOpcode(cc, opc_epilogue, INT_MAX);

  if ((mb->fb.access & ACC_SYNCHRONIZED) && !OPT_SETQ(OPT_IGNLOCK)) {
    if (mb->fb.access & ACC_STATIC)
      processAnOpcode(cc, opc_sync_static_exit, INT_MAX);
    else
      processAnOpcode(cc, opc_sync_obj_exit, INT_MAX);
  }

#ifndef USE_SSE2
  if (((mb->fb.access & ACC_STRICT) && !OPT_SETQ(OPT_IGNSTRICTFP)) ||
      OPT_SETQ(OPT_FRCSTRICTFP)) {
    if (!is_fpupc_double) {
      processAnOpcode(cc, opc_fppc_restore, INT_MAX);
    }

    processAnOpcode(cc, opc_strict_exit, INT_MAX);
  }
#endif	// !USE_SSE2

  processAnOpcode(cc, opc_methodtail, INT_MAX);

  return 0;
}


/*
 * returns: increment count for bytecode PC,
 *	    minus value if an exception occurred.
 */
static int processAnOpcode(CompilerContext *cc, int opcode, int byteoff) {
  struct methodblock *mb = cc->mb;
  cp_item_type *constant_pool = cbConstantPool(fieldclass(&mb->fb));
  unsigned char *type_table =
	constant_pool[CONSTANT_POOL_TYPE_TABLE_INDEX].type;

  int byteinc = 0;
  unsigned char *bytepc = mb->code + byteoff;
  int code_opcode;
  int32_t operand;
#ifdef COMPILE_DEBUG
  int compile_debug = cc->compile_debug;
#endif
  
#ifdef COMPILE_DEBUG
  if (compile_debug) {
    printf("procOpc(): %s(0x%02x,%d)\n",
	((opcode > opc_nonnull_quick) ? "(null)" : opcode_symbol[opcode]),
	opcode, opcode);
    printf("  b off: 0x%x(%d)\n", byteoff, byteoff);
    fflush(stdout);
  }
#endif

  // translate an opcode to the correspoinding internal opcode
  code_opcode = opcode;
  operand = -1;

proc_opc_switch:
  switch (code_opcode) {
  case opc_iconst_0:
  case opc_aconst_null:
  case opc_fconst_0:
    code_opcode = opc_iconst_0;  break;
  case opc_lconst_0:
  case opc_dconst_0:
    code_opcode = opc_lconst_0;  break;

  case opc_bipush:  case opc_sipush:
  case opc_ldc:  case opc_ldc_w:
  case opc_ldc_quick:  case opc_ldc_w_quick:
    // `code_opcode = opc_bipush' is located after the switch sentence.
    switch (code_opcode) {
    case opc_bipush:
      operand = (signed char)bytepc[1];	// signed
      break;
    case opc_sipush:
      operand = GET_INT16(bytepc + 1);
      break;
    case opc_ldc:
    case opc_ldc_quick:
      operand = bytepc[1];		// unsigned
      break;
    case opc_ldc_w:
    case opc_ldc_w_quick:
      operand = GET_UINT16(bytepc + 1);
      break;
    }
    code_opcode = opc_bipush;
    break;
  case opc_ldc2_w:  case opc_ldc2_w_quick:
    code_opcode = opc_ldc2_w;
    operand = GET_UINT16(bytepc + 1);
    break;

  case opc_iload:  case opc_fload:  case opc_aload:
    code_opcode = opc_iload;
    {
      if (*bytepc == opc_wide)
	operand = GET_UINT16(bytepc + 2);
      else
	operand = bytepc[1];
#ifdef COMPILE_DEBUG
      if (compile_debug) { printf("  index: %d\n", operand);  fflush(stdout); }
#endif
    }
    break;
  case opc_lload:  case opc_dload:
    code_opcode = opc_lload;
    {
      if (*bytepc == opc_wide)
	operand = GET_UINT16(bytepc + 2);
      else
	operand = bytepc[1];
#ifdef COMPILE_DEBUG
      if (compile_debug) { printf("  index: %d\n", operand);  fflush(stdout); }
#endif
    }
    break;

  case opc_iload_0:  case opc_fload_0:  case opc_aload_0:
    code_opcode = opc_iload;  operand = 0;
    break;
  case opc_iload_1:  case opc_fload_1:  case opc_aload_1:
    code_opcode = opc_iload;  operand = 1;
    break;
  case opc_iload_2:  case opc_fload_2:  case opc_aload_2:
    code_opcode = opc_iload;  operand = 2;
    break;
  case opc_iload_3:  case opc_fload_3:  case opc_aload_3:
    code_opcode = opc_iload;  operand = 3;
    break;

  case opc_lload_0:  case opc_dload_0:
    code_opcode = opc_lload;  operand = 0;
    break;
  case opc_lload_1:  case opc_dload_1:
    code_opcode = opc_lload;  operand = 1;
    break;
  case opc_lload_2:  case opc_dload_2:
    code_opcode = opc_lload;  operand = 2;
    break;
  case opc_lload_3:  case opc_dload_3:
    code_opcode = opc_lload;  operand = 3;
    break;

  case opc_iaload:  case opc_faload:  case opc_aaload:
    code_opcode = opc_iaload;  break;

  case opc_laload:  case opc_daload:
    code_opcode = opc_laload;  break;

  case opc_istore:  case opc_fstore:  case opc_astore:
    code_opcode = opc_istore;
    {
      if (*bytepc == opc_wide)
	operand = GET_UINT16(bytepc + 2);
      else
	operand = bytepc[1];
#ifdef COMPILE_DEBUG
      if (compile_debug) { printf("  index: %d\n", operand);  fflush(stdout); }
#endif
    }
    break;
  case opc_lstore:  case opc_dstore:
    code_opcode = opc_lstore;
    {
      if (*bytepc == opc_wide)
	operand = GET_UINT16(bytepc + 2);
      else
	operand = bytepc[1];
#ifdef COMPILE_DEBUG
      if (compile_debug) { printf("  index: %d\n", operand);  fflush(stdout); }
#endif
    }
    break;

  case opc_istore_0:  case opc_fstore_0:  case opc_astore_0:
    code_opcode = opc_istore;  operand = 0;
    break;
  case opc_istore_1:  case opc_fstore_1:  case opc_astore_1:
    code_opcode = opc_istore;  operand = 1;
    break;
  case opc_istore_2:  case opc_fstore_2:  case opc_astore_2:
    code_opcode = opc_istore;  operand = 2;
    break;
  case opc_istore_3:  case opc_fstore_3:  case opc_astore_3:
    code_opcode = opc_istore;  operand = 3;
    break;

  case opc_lstore_0:  case opc_dstore_0:
    code_opcode = opc_lstore;  operand = 0;
    break;
  case opc_lstore_1:  case opc_dstore_1:
    code_opcode = opc_lstore;  operand = 1;
    break;
  case opc_lstore_2:  case opc_dstore_2:
    code_opcode = opc_lstore;  operand = 2;
    break;
  case opc_lstore_3:  case opc_dstore_3:
    code_opcode = opc_lstore;  operand = 3;
    break;

  case opc_iastore:  case opc_fastore:
    code_opcode = opc_iastore;  break;
/*
  case opc_aastore:
    code_opcode = opc_aastore;  break;
*/
  case opc_lastore:  case opc_dastore:
    code_opcode = opc_lastore;  break;

  case opc_iinc:
    // code_opcode = opc_iinc;
    {
      if (*bytepc == opc_wide)
	operand = GET_UINT16(bytepc + 2);
      else
	operand = bytepc[1];
#ifdef COMPILE_DEBUG
      if (compile_debug) { printf("  index: %d\n", operand);  fflush(stdout); }
#endif
    }
    break;

  case opc_if_icmpeq:  case opc_if_acmpeq:
    code_opcode = opc_if_icmpeq;  break;
  case opc_if_icmpne:  case opc_if_acmpne:
    code_opcode = opc_if_icmpne;  break;

  case opc_ifeq:  case opc_ifnull:
    code_opcode = opc_ifeq;  break;
  case opc_ifne:  case opc_ifnonnull:
    code_opcode = opc_ifne;  break;

  case opc_goto:  case opc_goto_w:
    code_opcode = opc_goto;  break;
  case opc_jsr:  case opc_jsr_w:
    code_opcode = opc_jsr;  break;
  case opc_ret:
    // code_opcode = opc_ret;
    operand = bytepc[1];
    break;

#define ADJUST_ACCESS_STATIC(vop) \
    {\
      struct fieldblock *fb;\
      char *sig;\
      \
      operand = GET_UINT16(bytepc + 1);\
      \
      if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {\
	if (!ResolveClassConstantFromClass2(fieldclass(&mb->fb), operand,\
		cc->ee, 1 << CONSTANT_Fieldref, FALSE)) {\
	  JHandle *exc;\
	  sysAssert(exceptionOccurred(cc->ee));\
	  exc = cc->ee->exception.exc;\
	  sysAssert(exc != NULL);\
	  if (obj_classblock(exc) == classJavaLangNoClassDefFoundError) {\
	    exceptionClear(cc->ee);\
	    code_opcode = opc_throw_noclassdef;\
	    byteinc = 3;	/* opcode_length[{get,put}static] */\
	    break;\
	  }\
	  else if (obj_classblock(exc) == classJavaLangNoSuchFieldError) {\
	    exceptionClear(cc->ee);\
	    code_opcode = opc_throw_nofield;\
	    byteinc = 3;	/* opcode_length[{get,put}static] */\
	    break;\
	  }\
	  return -1;\
	}\
      }\
      \
      fb = constant_pool[operand].fb;\
      sig = fieldsig(fb);\
      if ((OPT_SETQ(OPT_CODEDB)) ||\
		!CB_INITIALIZED(fieldclass(fb))) {/* not quick instructions */\
	code_opcode =\
		((sig[0] == SIGNATURE_LONG) || (sig[0] == SIGNATURE_DOUBLE)) ?\
			opc_##vop##2 : opc_##vop;\
      }\
      else {	/* quick instructions */\
	code_opcode =\
		((sig[0] == SIGNATURE_LONG) || (sig[0] == SIGNATURE_DOUBLE)) ?\
			opc_##vop##2_quick : opc_##vop##_quick;\
      }\
    }

  case opc_getstatic:
  ADJUST_ACCESS_STATIC(getstatic);
  break;

  case opc_putstatic:
  ADJUST_ACCESS_STATIC(putstatic);
  break;

#define ADJUST_ACCESS_FIELD(vop) \
    {\
      struct fieldblock *fb;\
      char *sig;\
      \
      operand = GET_UINT16(bytepc + 1);\
      \
      if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {\
	if (!ResolveClassConstantFromClass2(fieldclass(&mb->fb), operand,\
		cc->ee, 1 << CONSTANT_Fieldref, FALSE)) {\
	  JHandle *exc;\
	  sysAssert(exceptionOccurred(cc->ee));\
	  exc = cc->ee->exception.exc;\
	  sysAssert(exc != NULL);\
	  if (obj_classblock(exc) == classJavaLangNoClassDefFoundError) {\
	    exceptionClear(cc->ee);\
	    byteinc = 3;	/* opcode_length[{get,put}field] */\
	    code_opcode = opc_throw_noclassdef;\
	    break;\
	  }\
	  else if (obj_classblock(exc) == classJavaLangNoSuchFieldError) {\
	    exceptionClear(cc->ee);\
	    byteinc = 3;	/* opcode_length[{get,put}field] */\
	    code_opcode = opc_throw_nofield;\
	    break;\
	  }\
	  return -1;\
	}\
      }\
      \
      fb = constant_pool[operand].fb;\
      sig = fieldsig(fb);\
      code_opcode =\
	((sig[0] == SIGNATURE_LONG) || (sig[0] == SIGNATURE_DOUBLE)) ?\
		opc_##vop##2 : opc_##vop;\
    }

  case opc_getfield:
  case opc_getfield_quick_w:
  ADJUST_ACCESS_FIELD(getfield);
  break;

  case opc_putfield:
  case opc_putfield_quick_w:
  ADJUST_ACCESS_FIELD(putfield);
  break;

  case opc_getfield_quick:
    code_opcode = opc_getfield;
    operand = bytepc[1];
    break;
  case opc_putfield_quick:
    code_opcode = opc_putfield;
    operand = bytepc[1];
    break;
  case opc_getfield2_quick:
    code_opcode = opc_getfield2;
    operand = bytepc[1];
    break;
  case opc_putfield2_quick:
    code_opcode = opc_putfield2;
    operand = bytepc[1];
    break;
  case opc_getstatic_quick:
  case opc_putstatic_quick:
#ifdef CODEDB
    if (OPT_SETQ(OPT_CODEDB)) {
      code_opcode += (opc_getstatic - opc_getstatic_quick);
    }
#endif
    operand = GET_UINT16(bytepc + 1);
    break;
  case opc_getstatic2_quick:
  case opc_putstatic2_quick:
#ifdef CODEDB
    if (OPT_SETQ(OPT_CODEDB)) {
      code_opcode += (opc_getstatic2 - opc_getstatic2_quick);
    }
#endif
    operand = GET_UINT16(bytepc + 1);
    break;

  case opc_inv_head:
  case opc_invoke_core:
  case opc_invoke_core_compiled:
  case opc_invokevirtual:
  case opc_invokevirtual_obj:
  case opc_invokespecial:
  case opc_inv_spe_varspace:
  case opc_invokestatic_quick:
  case opc_inv_stq_varspace:
  case opc_invokestatic:
  case opc_invokeinterface:
  case opc_invokeignored_static:
  case opc_invokeignored_static_quick:
#ifdef ELIMINATE_TAIL_RECURSION
  case opc_invoke_recursive:
#endif	// ELIMINATE_TAIL_RECURSION
    operand = GET_UINT16(bytepc + 1);
    break;

  case opc_new:
  case opc_new_quick:
  case opc_anewarray:
    // code_opcode = code_opcode;
    operand = GET_UINT16(bytepc + 1);
    if (code_opcode != opc_new_quick) {
      // check whether the class exists or not
      if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {
	if (!ResolveClassConstantFromClass2(fieldclass(&mb->fb), operand,
		cc->ee, 1 << CONSTANT_Class, FALSE)) {
	  JHandle *exc;
	  sysAssert(exceptionOccurred(cc->ee));
	  exc = cc->ee->exception.exc;
	  sysAssert(exc != NULL);
#ifdef COMPILE_DEBUG
	  fprintf(stderr, "cannot resolve: %s\n",
		cbName(exc->methods->classdescriptor));
#  if JDK_VER >= 12
	  printStackTrace(cc->ee, 100, NULL);
#  endif
	  fflush(stderr);
#endif	// COMPILE_DEBUG
	  if (obj_classblock(exc) == classJavaLangNoClassDefFoundError) {
	    exceptionClear(cc->ee);
	    byteinc = 3;	// opcode_length[{new,new_quick,anewarray}]
	    code_opcode = opc_throw_noclassdef;
	    break;
	  }
	  return -1;
	}
      }

      if (code_opcode == opc_new) {
	ClassClass *caller_cb = fieldclass(&mb->fb);
	ClassClass *cb = constant_pool[operand].clazz;
	int access = cbAccess(cb);

	if (access & (ACC_INTERFACE | ACC_ABSTRACT)) {
	  byteinc = 3;	// opcode_length[{new,new_quick,anewarray}]
	  code_opcode = opc_throw_instantiation;
	  break;
	}

#ifndef SLACK_ACCESS_CONTROL
	// access authority check
	// cur_mb may be not correct if METHOD_INLINING is defined
	if (!VerifyClassAccess(caller_cb, cb, TRUE)) {
	  byteinc = 3;	// opcode_length[{new,new_quick,anewarray}]
	  code_opcode = opc_throw_illegalaccess;
	  break;
	}
#endif	// SLACK_ACCESS_CONTROL

#if 0	// cannot translate `new' instructions to new_quick
	if (CB_INITIALIZED(constant_pool[operand].clazz)) {
	  code_opcode = opc_new_quick;
	}
#endif
      }
    }
    break;

  case opc_newarray:
    // code_opcode = opc_newarray;
    operand = bytepc[1];
#ifdef COMPILE_DEBUG
    if (compile_debug) { printf("  type: %d\n", operand);  fflush(stdout); }
#endif
    break;

  case opc_anewarray_quick:
    code_opcode = opc_anewarray;
    operand = GET_UINT16(bytepc + 1);
    break;
  case opc_multianewarray:
  case opc_multianewarray_quick:
    code_opcode = opc_multianewarray;
    operand = GET_UINT16(bytepc + 1);
    break;
  case opc_checkcast:
  case opc_checkcast_quick:
    code_opcode = opc_checkcast;
    operand = GET_UINT16(bytepc + 1);
    break;
  case opc_instanceof:
  case opc_instanceof_quick:
    code_opcode = opc_instanceof;
    operand = GET_UINT16(bytepc + 1);

    if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {
      if (!ResolveClassConstantFromClass2(fieldclass(&mb->fb), operand,
		cc->ee, 1 << CONSTANT_Class, FALSE)) {
	JHandle *exc = cc->ee->exception.exc;
	if (obj_classblock(exc) == classJavaLangNoClassDefFoundError) {
	  exceptionClear(cc->ee);
	  byteinc = 3;	// opcode_length[instanceof]
	  code_opcode = opc_throw_noclassdef;
	  break;
	}
      }
    }

    {
      ClassClass *dest_clazz = constant_pool[operand].clazz;
      if (dest_clazz == classJavaLangObject) {
	// omit
	return 3;	// opcode_length[opc_checkcast]
      }
    }

    break;

  case opc_epilogue:
    {
      int last_index;
      pcentry *entry1, *entry2;
      last_index = pctableLen(cc) - 1;
      entry1 = pctableGet(cc, last_index);

      if (entry1->opcode == opc_end) {	// not opc_exc_handler
	last_index--;
	entry2 = pctableGet(cc, last_index);

	if (entry2->opcode == opc_return) {
	  // copy byteoff to opc_end
	  entry1->increasing_byteoff = entry2->increasing_byteoff;
	  entry1->byteoff = entry2->byteoff;

	   pctableDelete(cc, last_index);
	}
      }
    }
    break;

  case opc_wide:
    code_opcode = *(bytepc + 1);
    goto proc_opc_switch;	// translate again
    break;

  default:
	// code_opcode = code_opcode
	break;
  }	/* switch (opcode) */


  // throwing IllegalAccessError if the access is not allowed
  {
    int opc_is_put = 0, opc_is_get = 0;

    switch (code_opcode) {
    case opc_getfield:  case opc_getfield2:
    case opc_getstatic:  case opc_getstatic2:
    case opc_getstatic_quick:  case opc_getstatic2_quick:
      opc_is_get = 1;  break;
    case opc_putfield:  case opc_putfield2:
    case opc_putstatic:  case opc_putstatic2:
    case opc_putstatic_quick:  case opc_putstatic2_quick:
      opc_is_put = 1;  break;
    }

    if (opc_is_get || opc_is_put) {
      struct fieldblock *fb;
      struct methodblock *src_mb;
      int fb_access;  ClassClass *fb_class;

      // the field must be already resolved above
      fb = constant_pool[operand].fb;

      if (!(src_mb = cc->mb)) {
#ifdef COMPILE_DEBUG
	printf("Illegal field access: methodblock is null.\n");
	fflush(stdout);
#endif
	byteinc = 3;	// opcode_length[{get,put}{field,static}]
	code_opcode = opc_throw_illegalaccess;
	goto check_access_done;
      }

      fb_access = fb->access;
      fb_class = fieldclass(fb);

#ifndef SLACK_ACCESS_CONTROL
#if JDK_VER >= 12
      // check private and protected from other packages
      if (!VerifyFieldAccess(
		fieldclass(&src_mb->fb), fb_class, fb_access, FALSE)) {
#ifdef COMPILE_DEBUG
	printf("Illegal field access: a private or protected field accessed.\n");
	printf("\tfrom: %s#%s\n",
		cbName(fieldclass(&src_mb->fb)), src_mb->fb.name);
	printf("\tto  : %s#%s\n", cbName(fb_class), fb->name);
	fflush(stdout);
#endif
	byteinc = 3;
	code_opcode = opc_throw_illegalaccess;
	goto check_access_done;
      }
#endif	// JDK_VER

      if (opc_is_put) {
	// check final
	if ((fb_access & ACC_FINAL) && (fieldclass(&src_mb->fb) != fb_class)) {
#ifdef COMPILE_DEBUG
	  printf("IllegalAccessError: A final field is accessed.\n");
	  fflush(stdout);
#endif
	  byteinc = 3;
	  code_opcode = opc_throw_illegalaccess;
	  goto check_access_done;
	}
      }
#endif	// SLACK_ACCESS_CONTROL
    }	// if (opc_is_get || opc_is_put)
  }
 check_access_done:


#ifdef COMPILE_DEBUG
  if (compile_debug) {
    if (code_opcode != opcode) {
      printf("  adj to 0x%02x(%d)\n",
		code_opcode, code_opcode);
      fflush(stdout);
    }
  }
#endif


  // register opcode, bytepc, state to the table
  pctableAdd(cc, code_opcode, operand, byteoff);


  // confirm whether the method may throw an exception
	// cc->may_throw should be initialized in execution of makePCTable()
  {
    CodeTable *codep = &code_table[code_opcode][0];
    unsigned char opc_flag =
	((unsigned char *)assembledCode + codep->offset)[-1];
    if (opc_flag & OPC_THROW_MASK)
      cc->may_throw = TRUE;
    if (opc_flag & OPC_JUMP_MASK) {
      cc->may_jump = TRUE;
    }
  }


  // update bytecode PC
  if ((byteinc == 0)		// byteinc has been not modified.
      && (opcode <= opc_nonnull_quick)) {	// not internal insn.
    switch (opcode) {
    case opc_tableswitch:
      {
	int32_t *argp = (int32_t *)ALIGNUP32(bytepc + 1);
	int32_t l = (int32_t)ntohl((uint32_t)argp[1]);
	int32_t h = (int32_t)ntohl((uint32_t)argp[2]);

	argp += 3 + (h - l + 1);
	byteinc = ((unsigned char *)argp) - bytepc;
      }
      break;
    case opc_lookupswitch:
      {
	int32_t *argp = (int32_t *)ALIGNUP32(bytepc + 1);
	int32_t npairs = (int32_t)ntohl((uint32_t)argp[1]);

	argp += 2 + (npairs * 2);
	byteinc = ((unsigned char *)argp) - bytepc;
      }
      break;
    case opc_wide:
      if (*(bytepc + 1) == opc_iinc)
	byteinc = 6;
      else
	byteinc = 4;
      break;
    default:
      byteinc = opcode_length[opcode];
      break;
    }	// switch (opcode)
  }	// if (opcode <= opc_nonnull_quick)

  return byteinc;
}


/*
 * Recognize basic blocks.
 */
static void makeBlockStructure(CompilerContext *cc) {
  pcentry *entry, *tgtentry, *next;
  unsigned char *bytepc;
  int opcode;
  int tgtpc;
  bool_t loopp;
  int i, j;
#ifdef COMPILE_DEBUG
  int compile_debug = cc->compile_debug;
#endif

#ifdef COMPILE_DEBUG
  if (compile_debug) {
    printf("makeBlockSt():\n");
    fflush(stdout);
  }
#endif

  for (i = 0; i < pctableLen(cc); i++) {
#define GET_NEXT \
      if (!(next = pctableNext(cc, entry)))  break

    entry = cc->pctable + i;
    opcode = entry->opcode;
    bytepc = cc->mb->code + entry->byteoff;

    if ((opc_ifeq <= opcode) && (opcode <= opc_jsr)) {	// if*, goto, jsr
      // jump offset
      if ((*bytepc == opc_goto_w) /*|| (*bytepc == opc_jsr_w)*/)
	tgtpc = GET_INT32(bytepc + 1);
      else
	tgtpc = GET_INT16(bytepc + 1);
      loopp = (tgtpc < 0) ? TRUE : FALSE;
      tgtpc += entry->byteoff;

#ifdef COMPILE_DEBUG
      if (compile_debug) {
	printf("  0x%x(%d): to 0x%x(%d)\n",
		entry->byteoff, entry->byteoff, tgtpc, tgtpc);
	fflush(stdout);
      }
#endif

      tgtentry = pctableGetByPC(cc, tgtpc);
      sysAssert(tgtentry != NULL);
      pcentrySetBlockHead(tgtentry);

      if (loopp) {
	pcentrySetLoopHead(tgtentry);
	pcentrySetLoopTail(entry);
#ifdef EXC_CHECK_IN_LOOP
	for (j = tgtpc; j < i; j++) {
	  int checked_opcode = cc->pctable[j].opcode;
	  if ((checked_opcode == opc_inv_head) ||
	      (checked_opcode == opc_athrow))
	    // don't generate exc_check insn
	    // if there is `inv_head' or `athrow' insn
	    goto exc_check_done;
	}

#  ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  insert exc_check\n");
	  fflush(stdout);
	}
#  endif
	pctableInsert(cc, i, opc_exc_check, -1/* operand */,
		entry->byteoff, -1/* state */, -1/* native off */);
	i++;
      exc_check_done:
	{}
#endif	// EXC_CHECK_IN_LOOP
      }
    }
    else if (opcode == opc_jsr) {
      GET_NEXT;
#ifdef COMPILE_DEBUG
      if (compile_debug) {
	printf("  jsr at 0x%x(%d)\n", entry->byteoff, entry->byteoff);
	fflush(stdout);
      }
#endif
      pcentrySetBlockHead(next);
      break;
    }
    else if (opcode == opc_tableswitch) {	// tableswitch
      int32_t *argp = (int32_t *)ALIGNUP32(bytepc + 1);
      int32_t defoff = (int32_t)ntohl((uint32_t)argp[0]);
      int32_t l = (int32_t)ntohl((uint32_t)argp[1]);
      int32_t h = (int32_t)ntohl((uint32_t)argp[2]);
#ifdef COMPILE_DEBUG
      if (compile_debug) {
	printf("  tableswitch at 0x%x(%d):\n", entry->byteoff, entry->byteoff);
	fflush(stdout);
      }
#endif
      loopp = (defoff < 0) ? TRUE : FALSE;
      defoff += entry->byteoff;

      tgtentry = pctableGetByPC(cc, defoff);
      sysAssert(tgtentry != NULL);
      pcentrySetBlockHead(tgtentry);
      if (loopp) {
	pcentrySetLoopHead(tgtentry);
	pcentrySetLoopTail(entry);
      }

      argp += 3;	// skip default, low, and high

      for (j = 0; j < (h - l + 1); j++) {
	int32_t off = (int32_t)ntohl((uint32_t)argp[j]);
	loopp = (off < 0) ? TRUE : FALSE;
	off += entry->byteoff;
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("   to 0x%x(%d)\n", off, off);
	  fflush(stdout);
	}
#endif
	tgtentry = pctableGetByPC(cc, off);
	sysAssert(tgtentry != NULL);
	pcentrySetBlockHead(tgtentry);
	if (loopp) {
	  pcentrySetLoopHead(tgtentry);
	  pcentrySetLoopTail(entry);
	}
      }
    }
    else if (opcode == opc_lookupswitch) {	// lookupswitch
      int32_t *argp = (int32_t *)ALIGNUP32(bytepc + 1);
      int32_t defoff = (int32_t)ntohl((uint32_t)argp[0]);
      int32_t npairs = (int32_t)ntohl((uint32_t)argp[1]);
#ifdef COMPILE_DEBUG
      if (compile_debug) {
	printf("  lookupswitch at 0x%x(%d):\n", entry->byteoff, entry->byteoff);
	fflush(stdout);
      }
#endif
      loopp = (defoff < 0) ? TRUE : FALSE;
      defoff += entry->byteoff;

      tgtentry = pctableGetByPC(cc, defoff);
      sysAssert(tgtentry != NULL);
      pcentrySetBlockHead(tgtentry);
      if (loopp) {
	pcentrySetLoopHead(tgtentry);
	pcentrySetLoopTail(entry);
      }

      argp += 2;	// skip default and npairs

      for (j = 0; j < npairs; j++) {
	int32_t off = (int32_t)ntohl((uint32_t)argp[1]);
	loopp = (off < 0) ? TRUE : FALSE;
	off += entry->byteoff;
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("   to 0x%x(%d)\n", off, off);
	  fflush(stdout);
	}
#endif
	tgtentry = pctableGetByPC(cc, off);
	sysAssert(tgtentry != NULL);
	pcentrySetBlockHead(tgtentry);
	if (loopp) {
	  pcentrySetLoopHead(tgtentry);
	  pcentrySetLoopTail(entry);
	}

	argp += 2;
      }
    }
  }
}


static void updateStates(CompilerContext *cc) {
  pcentry *pctable;
  int pctablelen = pctableLen(cc);
  CodeTable *codep;
  int opcode, state;
  int32_t operand;
  int i;
  struct methodblock *mb = cc->mb;
  cp_item_type *constant_pool = cbConstantPool(fieldclass(&mb->fb));
#ifdef METHOD_INLINING
  Stack *stack = newStack();
#endif
#ifdef COMPILE_DEBUG
  int compile_debug = cc->compile_debug;
#endif

#ifdef COMPILE_DEBUG
  if (compile_debug)
    printf("updateStates():\n");
#endif
  state = 0;
  for (i = 0; i < pctablelen; i++) {
    pctable = cc->pctable + i;
    opcode = pctable->opcode;
    codep = &code_table[opcode][state];

    switch (opcode) {
    // set current state
    case opc_return:
    case opc_epilogue:
      // for a statement which jump to return insn.
      state = 0;
      break;
    }

    pcentrySetState(pctable, state);
#ifdef COMPILE_DEBUG
    if (compile_debug) {
      printf("  %s(0x%02x,%d)\tst:%d\n",
	((opcode > opc_nonnull_quick) ? "(null)" : opcode_symbol[opcode]),
	opcode, opcode, state);
    }
#endif

    // set next state
    switch (opcode) {
    // jump instructions
    case opc_goto:
    case opc_ret:
    case opc_tableswitch:
    case opc_lookupswitch:
      state = STATE_AFTER_JUMP;
      break;
    case opc_jsr:
      state = STATE_AFTER_JSR;
      break;
    case opc_athrow:
      state = STATE_AFTER_ATHROW;
      break;

    // invocation instructions
    case opc_invoke_core:
    case opc_invoke_core_compiled:
    case opc_inlined_exit:
      {
	struct methodblock *method;
	CodeInfo *info;

	operand = pctable->operand;
	if (opcode == opc_inlined_exit)
	  method = mb;
	else
	  method = constant_pool[operand].mb;
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("    %s#%s %s\n", cbName(fieldclass(&method->fb)),
			method->fb.name, method->fb.signature);
	  fflush(stdout);
	}
#endif
	if (!(method->CompiledCodeInfo)) {
	  if (!prepareCompiledCodeInfo(cc->ee, method)) {
	    /* NOTREACHED */
	    fprintf(stderr, "FATAL: method has not been initialized:"
		"%s#%s %s.\n", cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature);
	    JVM_Exit(1);
	  }
	}
	info = (CodeInfo *)method->CompiledCodeInfo;
	sysAssert(info != NULL);

	// next state
	if (info->ret_size == 0)
	  state = 0;
	else if (info->ret_size == 1)
	  state = 1;
	else		// 2
	  state = 4;
      }
      break;

    default:
      if (codep->last_state != STSTA)
	state = codep->last_state;
      break;
    }	// switch (opcode)

#ifdef METHOD_INLINING
    switch (opcode) {
    case opc_inlined_enter:
      pushToStack(stack, (long)mb);
      mb = constant_pool[operand = pctable->operand].mb;
      constant_pool = cbConstantPool(fieldclass(&mb->fb));
#ifdef COMPILE_DEBUG
      if (compile_debug) {
	printf("    %s#%s %s\n", cbName(fieldclass(&mb->fb)),
		mb->fb.name, mb->fb.signature);
	fflush(stdout);
      }
#endif
      break;
    case opc_inlined_exit:
      mb = (struct methodblock *)popFromStack(stack);
      constant_pool = cbConstantPool(fieldclass(&mb->fb));
      break;
    }
#endif
  }

#ifdef METHOD_INLINING
  freeStack(stack);
#endif

#ifdef COMPILE_DEBUG
  if (compile_debug) {
    printf("updateStates() done\n");
    fflush(stdout);
  }
#endif
}


static int writeCode(CompilerContext *cc) {
  struct methodblock *mb = cc->mb;
  CodeInfo *info = (CodeInfo *)(mb->CompiledCodeInfo);
  cp_item_type *constant_pool = cbConstantPool(fieldclass(&mb->fb));
  unsigned char *type_table =
	constant_pool[CONSTANT_POOL_TYPE_TABLE_INDEX].type;

  pcentry *pctable;
  int opcode, state;
  int32_t operand;
  int32_t nativeoff = 0, nextoff = 0;
  unsigned char *bytepc;
  int ret = 0;
  int i;

  CodeTable *codep;
  unsigned char *bufp;  int insn_head_off;
#ifdef METHOD_INLINING
  Stack *stack = newStack();
#endif
#ifdef COMPILE_DEBUG
  int compile_debug = cc->compile_debug;
#endif

#ifdef COMPILE_DEBUG
  if (compile_debug) {
    printf("writeCode called: %s#%s %s.\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
    printf("  type_table: 0x%x\n", (int)type_table);
    fflush(stdout);
  }
#endif

  sysAssert(info != NULL);

  for (i = 0; i < pctableLen(cc); i++) {
    pctable = cc->pctable + i;
    opcode = pctable->opcode;
    operand = pctable->operand;
    state = pcentryState(pctable);
    bytepc = mb->code + pctable->byteoff;

#ifdef COMPILE_DEBUG
    if (compile_debug) {
      printf("writeCode(): %s(0x%02x,%d) st:%d\n",
	((opcode > opc_nonnull_quick) ? "(null)" : opcode_symbol[opcode]),
	opcode, opcode, state);
      printf("  off: b 0x%x(%d),0x%x(%d) n 0x%x(%d)\n",
		pctable->byteoff, pctable->byteoff,
		pctable->increasing_byteoff, pctable->increasing_byteoff,
		nativeoff, nativeoff);
      fflush(stdout);
    }
#endif


#ifdef ALIGN_JUMP_TARGET
    // align a jump target to 16-byte boundary
#define ALIGN_BOUND	16
#define JUMP_THRESHOLD	4
		// have to >= 2
    if (pcentryLoopHead(pctable)) {
      int pad =
	((-nativeoff - (ALIGN_BOUND-1)) % ALIGN_BOUND) + (ALIGN_BOUND-1);
#ifdef COMPILE_DEBUG
      if (compile_debug) {
	if (pad != 0) {
	  printf("  pad: %d\n", pad);
	  fflush(stdout);
	}
      }
#endif
      sysAssert((0 <= pad) && (pad < ALIGN_BOUND));

      if (pad >= JUMP_THRESHOLD) {
	char code_pad[2];

	pad -= 2;
	code_pad[0] = (unsigned char)0xeb;
	code_pad[1] = (unsigned char)pad;

	writeToBuffer(cc, code_pad, 2);
	nativeoff += 2;
      }

      {
	static unsigned char nops[] = {
#if ALIGN_BOUND > 8
		0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
#if ALIGN_BOUND > 16
		0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
#if ALIGN_BOUND > 24
		0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
#endif
#endif
#endif
		0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
	writeToBuffer(cc, nops, pad);
	nativeoff += pad;
      }
    }
#endif	// ALIGN_JUMP_TARGET


    codep = &code_table[opcode][state];
#ifdef COMPILE_DEBUG
    if (compile_debug) {
      printf("  len native: 0x%x(%d)\n", codep->length, codep->length);
      fflush(stdout);
    }
#endif

    if (opcode == opc_exc_handler) {
      if (!(info->exc_handler_nativeoff))	// not established yet
	info->exc_handler_nativeoff = nativeoff;
    }
    else if (opcode == opc_epilogue) {
      info->finish_return_nativeoff = nativeoff;
      if (!(info->exc_handler_nativeoff))	// not established yet
	info->exc_handler_nativeoff = nativeoff;
    }

    insn_head_off = cc->bufp - cc->buffer;
	// save to resolve constants

    // write native code to buffer
    {
      unsigned char *nativecode =
		(unsigned char *)assembledCode + codep->offset;
      writeToBuffer(cc, nativecode, codep->length);
      pctable->nativeoff = nativeoff;

#ifdef EXC_BY_SIGNAL
      // treat throw table
      if (nativecode[-1] & OPC_SIGNAL_MASK) {
	// these native code may send SIGSEGV or SIGFPE
	throwtableAdd(cc, info,
		nativeoff, (uint16_t)codep->length,
		(uint16_t)pctable->increasing_byteoff);
      }
#endif	// EXC_BY_SIGNAL
    }


#ifdef PATCH_WITH_SIGTRAP
    {
      unsigned char *nativep = cc->buffer + nativeoff;
      throwentry *tentry;

      switch (opcode) {	// internal opcode
      case opc_getstatic:  case opc_getstatic2:
      case opc_putstatic:  case opc_putstatic2:
      case opc_invokestatic:
      case opc_invokeignored_static:
      case opc_new:
#  ifdef METHOD_INLINING
      case opc_inlined_enter:
#  endif
	{
	  ClassClass *cb;

	  // refuse _quick instructions
#  ifdef METHOD_INLINING
	  if (opcode == opc_inlined_enter) {
	    if (*bytepc != opc_invokestatic)
	      break;
	  }
	  else
#  endif
	  if ((*bytepc == opc_new_quick) ||
	      ((*bytepc >= opc_getstatic_quick) &&
	       (*bytepc <= opc_putstatic2_quick)) ||
	      (*bytepc == opc_invokeignored_quick))
	    break;

	  if (opcode == opc_new) {
	    cb = constant_pool[operand].clazz;
	  }
	  else {
	    struct methodblock *invoke_mb;

	    invoke_mb = constant_pool[operand].mb;
	    cb = fieldclass(&invoke_mb->fb);

	    // cannot omit invocation of once_InitClass()
	    // if the class has not been init'd (in case operand is opc_new)
	    if (CB_INITIALIZED(cb))
	      break;
	  }
	  sysAssert(cb != NULL);

	  tentry = throwtableAdd(cc, info,
		nativeoff, (uint16_t)codep->length,
		(uint16_t)pctable->increasing_byteoff);
	  // patch
	  tentry->opcode = (uint16_t)opcode;
	  tentry->cb = cb;
	  tentry->patched_code = nativep[0];
	  nativep[0] = 0xcc;	// int $3
	}

#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  patch on 0x%02x(%d)\n", opcode, opcode);
	  fflush(stdout);
	}
#endif
	break;

      case opc_invokeinterface:
	// refuse invokeinterface_quick
	if (*bytepc == opc_invokeinterface_quick)  break;

	tentry = throwtableAdd(cc, info,
		nativeoff, (uint16_t)codep->length,
		(uint16_t)pctable->increasing_byteoff);

	// patch on the next instruction of
	// the invocation to getInterfaceMethod
#ifdef INVINTF_INLINE_CACHE
#  ifdef RUNTIME_DEBUG
#    if GCC_VER >= 296
#      define INVINTF_PATCH_OFFSET	0x62
#    else
#      define INVINTF_PATCH_OFFSET	0x64
#    endif	// GCC_VER
#  else
#    define INVINTF_PATCH_OFFSET	0x30
#  endif
#else
#  ifdef RUNTIME_DEBUG
#    if GCC_VER >= 296
#      define INVINTF_PATCH_OFFSET	0x46
#    else
#      define INVINTF_PATCH_OFFSET	0x48
#    endif	// GCC_VER
#  else
#    define INVINTF_PATCH_OFFSET	0x14
#  endif
#endif
	tentry->opcode = (uint16_t)opcode;
	tentry->patched_code = nativep[INVINTF_PATCH_OFFSET];
	nativep[INVINTF_PATCH_OFFSET] = 0xcc;	// int $3
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  patch on 0x%02x(%d)\n", opcode, opcode);
	  fflush(stdout);
	}
#endif
	break;
      }
    }
#endif	// PATCH_WITH_SIGTRAP


    nextoff = nativeoff + codep->length;


    // register jump instructions to table
    // and update nextoff.
    if ((opc_ifeq <= opcode) && (opcode <= opc_ret)) {
	/* internal opcodes:
	   ifeq, ifne, iflt, ifge, ifgt, ifle,
	   if_icmpeq, if_icmne, if_icmlt, if_icmge, if_icmgt, if_icmle,
	   if_acmpeq, if_acmpne,
	   goto, jsr, ret */
      int tgtoff = -1;
      int tgtstate;
      int last_state;
      CodeTable *transcodep;

      last_state = code_table[opcode][state].last_state;
      if (last_state == STSTA)  last_state = state;

#ifdef COMPILE_DEBUG
      if (compile_debug) {
	printf("  jump instruction: %s(0x%x,%d)\n",
		opcode_symbol[*bytepc], *bytepc, *bytepc);
	printf("    last state: %d\n", last_state);
	fflush(stdout);
      }
#endif
      if (opcode == opc_ret) {
	tgtstate = STATE_AFTER_JSR;
      }
      else {
	int jumpoff;
	pcentry *tgttable;

	if ((*bytepc == opc_goto_w) || (*bytepc == opc_jsr_w))
	  jumpoff = GET_INT32(bytepc + 1);
	else
	  jumpoff = GET_INT16(bytepc + 1);
	tgtoff = pctable->byteoff + jumpoff;
	tgttable = pctableGetByPC(cc, tgtoff);
	sysAssert(tgttable != NULL);
	tgtstate = pcentryState(tgttable);
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  int tgtop = tgttable->opcode;
	  printf("    target %s(0x%x,%d) offset: 0x%x(%d), state: %d\n",
		((tgtop > opc_nonnull_quick)?"(null)":opcode_symbol[tgtop]),
		tgtop, tgtop, tgtoff, tgtoff, tgtstate);
	  fflush(stdout);
	}
#endif
      }
      transcodep = &code_table[opc_stateto0 + tgtstate][last_state];

      if ((opc_ifeq <= opcode) && (opcode <= opc_if_acmpne))
	/* || ((opc_ifnull <= opcode) && (opcode <= opc_ifnonnull)) */ {
	int rop = 0, rop_rev = 0;

	switch (opcode) {
#define JP_CASE(OP, ROP4, ROP_REV1) \
	case opc_if##OP:\
	case opc_if_icmp##OP:\
	  rop = ROP4;  rop_rev = ROP_REV1;\
	  break
	// 0x0f, ROP4 takes 4 byte operand, ROP_REV1 takes 1 byte.

	  JP_CASE(eq, 0x84/*je*/, 0x75/*jne*/);
	  JP_CASE(ne, 0x85/*jne*/, 0x74/*je*/);
	  JP_CASE(lt, 0x8c/*jl*/, 0x7d/*jge*/);
	  JP_CASE(ge, 0x8d/*jge*/, 0x7c/*jl*/);
	  JP_CASE(gt, 0x8f/*jg*/, 0x7e/*jle*/);
	  JP_CASE(le, 0x8e/*jle*/, 0x7f/*jg*/);
	}

	if (last_state == tgtstate) {
	  unsigned char code_jp[] = { 0x0f, rop, 0, 0, 0, 0 };
	  // jxx X(4byte)
	  writeToBuffer(cc, code_jp, 6);
	  nextoff += 6;
	}
	else {
	  // jXX (length_of_stateTo0 + 5)
	  {
	    unsigned char code_jp[] = { rop_rev, transcodep->length + 5 };
	    writeToBuffer(cc, code_jp, 2);
	    nextoff += 2;
	  }
	  // code_statetoX
	  pctableInsert(cc, i + 1,
		opc_stateto0 + tgtstate, -1/* operand */, pctable->byteoff,
		last_state, nextoff);
	  i++;
	  writeToBuffer(cc,
		(unsigned char *)assembledCode + transcodep->offset,
		transcodep->length);
	  nextoff += transcodep->length;

	  // jmp X(4byte)
	  {
	    unsigned char code_jp[] = { 0xe9, 0, 0, 0, 0 };
	    writeToBuffer(cc, code_jp, 5);
	    nextoff += 5;
	  }
	}

	jptableAdd(cc, tgtoff, nextoff - 4);
      }
      else {
	// internal opcodes: goto, jsr, ret
	// code_sattetoX
	pctableInsert(cc, i + 1,
		opc_stateto0 + tgtstate, -1/* operand */, pctable->byteoff,
		last_state, nextoff);
	i++;
	writeToBuffer(cc, (unsigned char *)assembledCode + transcodep->offset,
			transcodep->length);
	nextoff += transcodep->length;

	if (opcode != opc_ret) {	// goto, jsr
	  // jmp X(4byte)
	  unsigned char code_jp[] = { 0xe9, 0, 0, 0, 0 };
	  writeToBuffer(cc, code_jp, 5);
	  nextoff += 5;

	  jptableAdd(cc, tgtoff, nextoff - 4);
	}
	else {	// ret
	  // jmp *%eax
	  unsigned char code_jp[] = { 0xff, 0xe0 };
	  writeToBuffer(cc, code_jp, 2);
	  nextoff += 2;
	}
      }
    }	// if jump instruction


    // resolve constants
    bufp = cc->buffer + insn_head_off;

    switch (opcode) {
    case opc_bipush:
      {
	int orig_opc = *bytepc;

	if ((orig_opc == opc_ldc) || (orig_opc == opc_ldc_w) ||
	    (orig_opc == opc_ldc_quick) || (orig_opc == opc_ldc_w_quick)) {
	  if ((orig_opc == opc_ldc) || (orig_opc == opc_ldc_w)) {
	    sysAssert(operand != -1);
	    // resolve
	    if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand))
	      if (!ResolveClassConstantFromClass2(
			fieldclass(&mb->fb), operand, cc->ee,
			(1 << CONSTANT_Integer) | (1 << CONSTANT_Float) |
			(1 << CONSTANT_String), FALSE)) {
		ret = 1;
		goto writecode_done;
	      }
	  }
	  if (CONSTANT_POOL_TYPE_TABLE_GET_TYPE(type_table, operand) !=
		CONSTANT_String) {
	    int32_t val = constant_pool[operand].i;
#ifdef COMPILE_DEBUG
	    if (compile_debug) {
	      printf("  index: %d\n", operand);
	      if (CONSTANT_POOL_TYPE_TABLE_GET_TYPE(type_table, operand) ==
			CONSTANT_Integer)
		printf("  value: %d\n", val);
	      else
		printf("  value: %e(float)\n", val);
	      fflush(stdout);
	    }
#endif
	    memcpy(bufp + constant_table[opcode][state][0], &val, 4);
	  }
	}
	else {	// other than opc_ldc*
#ifdef COMPILE_DEBUG
	  if (compile_debug) {
	    printf("  value: %d\n", operand);  fflush(stdout);
	  }
#endif
	  memcpy(bufp + constant_table[opcode][state][0], &operand, 4);
	}
      }
      break;

    case opc_ldc2_w:
      {
	int32_t cp_entry[2];
	sysAssert(operand != -1);
#ifdef COMPILE_DEBUG
	if (compile_debug) { printf("  index: %d\n", operand); fflush(stdout);}
#endif

	if (*bytepc == opc_ldc2_w) {	// resolve
	  if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {
	    if (!ResolveClassConstantFromClass2(
		fieldclass(&mb->fb), operand, cc->ee,
		(1 << CONSTANT_Long) | (1 << CONSTANT_Double), FALSE)) {
	      ret = 1;
	      goto writecode_done;
	    }
	  }
	}
	cp_entry[0] = constant_pool[operand].i;
	cp_entry[1] = constant_pool[operand + 1].i;
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  value: %lld, %15g\n",
		*((long long *)cp_entry), *((double *)cp_entry));
	  fflush(stdout);
	}
#endif

	memcpy(bufp + constant_table[opcode][state][0], cp_entry + 1, 4);
	memcpy(bufp + constant_table[opcode][state][1], cp_entry, 4);
      }
      break;

    case opc_iload:  case opc_fload_fld:
    case opc_istore:  case opc_istld:  case opc_fst_fstore:
    case opc_ret:
      {
	int32_t index;
	sysAssert(operand != -1);
#ifdef COMPILE_DEBUG
	if (compile_debug) { printf("  index: %d\n", operand); fflush(stdout);}
#endif

	index = operand * -4;
	sysAssert(constant_table[opcode][state][0] != 0);
	memcpy(bufp + constant_table[opcode][state][0], &index, 4);
#ifdef RUNTIME_DEBUG
	if ((opcode != opc_fload_fld) && (opcode != opc_fst_fstore)) {
	  memcpy(bufp + constant_table[opcode][state][1], &operand, 4);
	}
#endif
      }
      break;

    case opc_lload:  case opc_dload_dld:
    case opc_lstore:  case opc_lstld:  case opc_dst_dstore:
      {
	int32_t index;
	sysAssert(operand != -1);
#ifdef COMPILE_DEBUG
	if (compile_debug) { printf("  index: %d\n", operand); fflush(stdout);}
#endif

	index = (operand + 1) * -4;
	memcpy(bufp + constant_table[opcode][state][0], &index, 4);
	if ((opcode != opc_dload_dld) && (opcode != opc_dst_dstore)) {
	  index += 4;
	  memcpy(bufp + constant_table[opcode][state][1], &index, 4);
#ifdef RUNTIME_DEBUG
	  memcpy(bufp + constant_table[opcode][state][2], &operand, 4);
#endif
	}
      }
      break;

#if defined(METAVM) && !defined(METAVM_NO_ARRAY)
    case opc_iaload:  case opc_faload_fld:
    case opc_baload:
    case opc_caload:
    case opc_saload:
      {
	int32_t *p = (int32_t *)(bufp + constant_table[opcode][state][0]);
	if (*bytepc == opc_aaload) {
	  *p = (int32_t)1;
	}
	else
	  *p = (int32_t)0;
      }
      break;

    case opc_iastore:	// including fastore
    case opc_bastore:
    case opc_castore:
    case opc_sastore:
      {
	int32_t *p = (int32_t *)(bufp + constant_table[opcode][state][0]);
	*p = (int32_t)0;
      }
      break;

    case opc_aastore:
      {
	int32_t *p = (int32_t *)(bufp + constant_table[opcode][state][0]);
	*p = (int32_t)1;
      }
      break;
#endif	// METAVM_NO_ARRAY

    case opc_iinc:
      {
	int32_t index, constbyte;
	sysAssert(operand != -1);
	
	if (*bytepc == opc_wide) {
	  constbyte = GET_INT16(bytepc + 4);
	}
	else {
	  constbyte = (signed char)bytepc[2];
	}
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  vars[%d] += %d\n", operand, constbyte);  fflush(stdout);
	}
#endif
	index = operand * -4;
#ifdef RUNTIME_DEBUG
	memcpy(bufp + constant_table[opcode][state][2], &index, 4);
	memcpy(bufp + constant_table[opcode][state][2] + 4, &constbyte, 4);

	// for IINC_DEBUG1
	memcpy(bufp + constant_table[opcode][state][0], &index, 4);
	memcpy(bufp + constant_table[opcode][state][1], &operand, 4);

	// for IINC_DEBUG2
	memcpy(bufp + constant_table[opcode][state][3], &index, 4);
	memcpy(bufp + constant_table[opcode][state][4], &constbyte, 4);
#else
	memcpy(bufp + constant_table[opcode][state][0], &index, 4);
	memcpy(bufp + constant_table[opcode][state][0] + 4, &constbyte, 4);
#endif
      }
      break;

    case opc_jsr:
#ifdef COMPILE_DEBUG
      if (compile_debug) {
	printf("  next native offset: 0x%x(%d)\n", nextoff, nextoff);
	fflush(stdout);
      }
#endif
      memcpy(bufp + constant_table[opcode][state][0], &nextoff, 4);
      break;

    case opc_tableswitch:
      {
	int32_t *argp = (int32_t *)ALIGNUP32(bytepc + 1);
	int32_t l = (int32_t)ntohl((uint32_t)argp[1]);
	int32_t h = (int32_t)ntohl((uint32_t)argp[2]);
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  low: %d, high: %d\n", l, h);  fflush(stdout);
	}
#endif
	memcpy(bufp + constant_table[opcode][state][0], &l, 4);
	memcpy(bufp + constant_table[opcode][state][1], &h, 4);
      }
      break;

    case opc_lookupswitch:
      {
	int32_t *argp = (int32_t *)ALIGNUP32(bytepc + 1);
	int32_t npairs = (int32_t)ntohl((uint32_t)argp[1]);
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  npairs: %d\n", npairs);  fflush(stdout);
	}
#endif
	memcpy(bufp + constant_table[opcode][state][0], &npairs, 4);
      }
      break;

#ifdef COMPILE_DEBUG
#  define CONST_GETMEMBER_DEBUG1 \
	if (compile_debug) {\
	  printf("  %s#%s\n", cbName(fb->clazz), fb->name);\
	  fflush(stdout);\
	}
#  define CONST_GETMEMBER_DEBUG2 \
	if (compile_debug) {\
	  printf("  slot: %d\n", slot); fflush(stdout);\
	}
#else
#  define CONST_GETMEMBER_DEBUG1
#  define CONST_GETMEMBER_DEBUG2
#endif

#ifdef RUNTIME_DEBUG
#  define CONST_GETMEMBER_DEBUG3\
  memcpy(bufp + constant_table[opcode][state][1], &slot, 4)
#else
#  define CONST_GETMEMBER_DEBUG3
#endif

#ifdef METAVM
	// supply info. whether if field is object to generated native code
#  define METAVM_CONST_GETMEMBER(vop) \
	if (opcode == opc_##vop) {\
	  int32_t objp;\
	  switch (*fb->signature) {\
	  case 'L':  case '[':\
	    objp = 1;\
	    break;\
	  default:\
	    objp = 0;\
	    break;\
	  }\
	  memcpy(bufp + constant_table[opcode][state][1], &objp, 4);\
	}
#else
#  define METAVM_CONST_GETMEMBER(vop)
#endif	// METAVM

#ifdef RUNTIME_DEBUG
#  define RUNTIME_DEBUGP	1
#else
#  define RUNTIME_DEBUGP	0
#endif
#ifdef METAVM
#  define METAVMP	1
#else
#  define METAVMP	0
#endif	// METAVM

#define CONST_GETMEMBER(vop) \
    case opc_##vop:\
    case opc_##vop##2:\
      {\
	struct fieldblock *fb = NULL;\
	int32_t slot;\
	sysAssert(operand != -1);\
	\
	if ((*bytepc == opc_##vop##_quick) || (*bytepc == opc_##vop##2_quick))\
		/* after lossy translation (which is suppressed) */\
	  slot = operand;\
	else {	/* ..field, ..field_quick_w */\
	  if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {\
	    if (!ResolveClassConstantFromClass2(\
		fieldclass(&mb->fb), operand, cc->ee,\
		1 << CONSTANT_Fieldref, FALSE))\
	      ret = 1;\
	      goto writecode_done;\
	  }\
	  fb = constant_pool[operand].fb;\
	  CONST_GETMEMBER_DEBUG1;\
	  slot = fb->u.offset / sizeof(OBJECT);\
	}\
	CONST_GETMEMBER_DEBUG2;\
	memcpy(bufp + constant_table[opcode][state][0], &slot, 4);\
	if (RUNTIME_DEBUGP)\
	  if (METAVMP && (opcode == opc_##vop))\
	    memcpy(bufp + constant_table[opcode][state][2], &slot, 4);\
	  else\
	    memcpy(bufp + constant_table[opcode][state][1], &slot, 4);\
        CONST_GETMEMBER_DEBUG3;\
	\
	METAVM_CONST_GETMEMBER(vop)\
      }\
      break

    CONST_GETMEMBER(getfield);
    CONST_GETMEMBER(putfield);

    case opc_inv_head:
      {
	struct methodblock *method = constant_pool[operand].mb;
	int32_t args_size = method->args_size;

#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  %s#%s %s\n", cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature);
	  fflush(stdout);
	}
#endif

	memcpy(bufp + constant_table[opcode][state][0], &args_size, 4);
      }
      break;

    case opc_invoke_core:
    case opc_invoke_core_compiled:
      {
	struct methodblock *method = constant_pool[operand].mb;
	int32_t args_size = method->args_size;

#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  args_size: %d\n", args_size);
	  fflush(stdout);
	}
#endif

	memcpy(bufp + constant_table[opcode][state][0], &args_size, 4);
      }
      break;

    case opc_invokevirtual:
    case opc_invokevirtual_obj:
      {
	struct methodblock *method = constant_pool[operand].mb;
	uint32_t slot = method->fb.u.offset;

#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  0x%x %s#%s %s\n", method,
		cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature);
	  printf("  offset in table: %d\n", method->fb.u.offset);
	  fflush(stdout);
	}
#endif

	memcpy(bufp + constant_table[opcode][state][0], &slot, 4);
      }
      break;

    case opc_inv_spe_varspace:
    case opc_inv_stq_varspace:
      {
	struct methodblock *method = constant_pool[operand].mb;
	int32_t localvar_space;

	localvar_space = method->nlocals - method->args_size;
	if (localvar_space > 0)
	  localvar_space *= 4;
	else
	  localvar_space = 0;	// nlocals is 0 in the case of native method

	memcpy(bufp + constant_table[opcode][state][0], &localvar_space, 4);
      }
      break;

#ifdef ELIMINATE_TAIL_RECURSION
    case opc_invoke_recursive:
    case opc_invoke_recursive_1:
    case opc_invoke_recursive_2:
    case opc_invoke_recursive_3:
      {
	int const_off;
	pcentry *entry_start;
	int j;
	uint32_t arg_off, tgt_off;

	if (opcode == opc_invoke_recursive) {
	  struct methodblock *method = constant_pool[operand].mb;
	  int32_t args_size = method->args_size;

	  memcpy(bufp + constant_table[opcode][state][0], &args_size, 4);
	  const_off = 1;
	}
	else
	  const_off = 0;

	// search opc_start
	for (j = 0; j < pctableLen(cc); j++) {
	  entry_start = pctable = cc->pctable + j;
	  if (entry_start->opcode == opc_start)
	    break;
	}
	sysAssert(j < pctableLen(cc));

	arg_off = insn_head_off + constant_table[opcode][state][const_off];
	tgt_off = entry_start->nativeoff;

	tgt_off -= arg_off;
	tgt_off -= 4;

	memcpy(bufp + constant_table[opcode][state][const_off], &tgt_off, 4);
      }
      break;
#endif	// ELIMINATE_TAIL_RECURSION

#ifdef METHOD_INLINING
    case opc_inlined_enter:
      {
	// resolve constants
	struct methodblock *method = constant_pool[operand].mb;
	int32_t arg0 = 4 * (method->args_size - 1);
	int32_t arg1 = -4 * (method->nlocals - 1);

	memcpy(bufp + constant_table[opcode][state][0], &arg0, 4);
	memcpy(bufp + constant_table[opcode][state][1], &arg1, 4);
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  nlocals, args_size: %d, %d\n",
		method->nlocals, method->args_size);
	}
#endif

	// maintain methodblock
	pushToStack(stack, (long)mb);
	mb = method;
		// info must not be maintained
	constant_pool = cbConstantPool(fieldclass(&mb->fb));
	type_table = constant_pool[CONSTANT_POOL_TYPE_TABLE_INDEX].type;
      }
      break;

    case opc_inlined_exit:
      {
	struct methodblock *method;
	int32_t arg;

	// maintain methodblock
	mb = (struct methodblock *)popFromStack(stack);
		// info must not be maintained
	constant_pool = cbConstantPool(fieldclass(&mb->fb));
	type_table = constant_pool[CONSTANT_POOL_TYPE_TABLE_INDEX].type;

	// resolve constants
	method = constant_pool[operand].mb;
	sysAssert(method->nlocals >= method->args_size);
	arg = -4 * (method->nlocals);

	memcpy(bufp + constant_table[opcode][state][0], &arg, 4);
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  nlocals, args_size: %d, %d\n",
		method->nlocals, method->args_size);
	}
#endif
      }
      break;
#endif	// METHOD_INLINING

    case opc_newarray:
      {
	sysAssert(operand != -1);
	memcpy(bufp + constant_table[opcode][state][0], &operand, 4);
      }
      break;

    case opc_multianewarray:
      {
	int32_t dimensions = bytepc[3];
	sysAssert(operand != -1);
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  index: %d\n  dimensions: %d\n", operand, dimensions);
	  fflush(stdout);
	}
#endif
	memcpy(bufp + constant_table[opcode][state][0], &dimensions, 4);
      }
      break;

    case opc_invokeignored_quick:
      {
	int32_t args_size = bytepc[1];
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  args_size: %d\n", args_size);  fflush(stdout);
	}
#endif
	memcpy(bufp + constant_table[opcode][state][0], &args_size, 4);
      }
      break;
    case opc_invokeignored_static:
    case opc_invokeignored_static_quick:
      {
	struct methodblock *method;
	int32_t args_size;

	sysAssert(operand != -1);

	if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand))
	    if (!ResolveClassConstantFromClass2(
		fieldclass(&mb->fb), operand, cc->ee,
		(1 << CONSTANT_Methodref) | (1 << CONSTANT_InterfaceMethodref),
		FALSE)) {
	      ret = 1;
	      goto writecode_done;
	    }

	method = constant_pool[operand].mb;
	args_size = method->args_size;

#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  %s#%s %s\n", cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature);
	  printf("  args_size: %d\n", args_size);
	  fflush(stdout);
	}
#endif
#if (defined(INITCLASS_IN_COMPILATION) && !defined(PATCH_ON_JUMP)) || defined(PATCH_WITH_SIGTRAP)
	memcpy(bufp + constant_table[opcode][state][0], &args_size, 4);
#else
	if (opcode == opc_invokeignored_static)
	  memcpy(bufp + constant_table[opcode][state][1], &args_size, 4);
	else	// invokeignroed_static_quick
	  memcpy(bufp + constant_table[opcode][state][0], &args_size, 4);
#endif
      }
      break;

#ifdef METAVM
    case opc_inv_metavm:
      {
	pcentry *succ_pctable;
	int succ_opcode;
	CodeTable *succ_codep;

	extern void invoke_core(), invoke_core_done();
#  ifdef EAGER_COMPILATION
	extern void invoke_core_compiled(), invoke_core_compiled_done();
#  endif

	// search opc_invoke_core
	int j = i + 1;	// points to the next instruction of inv_metavm
	int jump_arg = 0;
	while (TRUE) {
	  succ_pctable = cc->pctable + j;
	  succ_opcode = succ_pctable->opcode;
	  if (succ_opcode == opc_invoke_core) {
	    jump_arg += (((char *)invoke_core_done) - ((char *)invoke_core));
	    break;
	  }
#  ifdef EAGER_COMPILATION
	  if (succ_opcode == opc_invoke_core_compiled) {
	    jump_arg += (((char *)invoke_core_compiled_done) -
			((char *)invoke_core_compiled));
	    break;
	  }
#  endif
	  succ_codep =
		&code_table[succ_opcode][pcentryState(succ_pctable)];
#ifdef COMPILE_DEBUG
	  if (compile_debug) {
	    printf("  len of insn 0x%x: %d\n", succ_codep->length);
	    fflush(stdout);
	  }
#endif
	  jump_arg += succ_codep->length;
	  j++;
	}

#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  jump_arg: %d\n", jump_arg);
	  fflush(stdout);
	}
#endif
	memcpy(bufp + constant_table[opcode][state][0], &jump_arg, 4);
      }
      break;
#endif	// METAVM
    }	// resolve constants: switch (opcode)

    // update native offset
    nativeoff = nextoff;
  }	// for (i = 0; i < pctableLen(cc); i++)


writecode_done:

#ifdef METHOD_INLINING
  freeStack(stack);
#endif

#ifdef EXC_BY_SIGNAL
  if (info->throwtablelen == 0) {
    if (info->throwtable != NULL)  sysFree(info->throwtable);
    info->throwtablesize = 0;
  }
  else if (info->throwtablelen < info->throwtablesize) {	// not equal
    // copy throwtable to shrink it
    info->throwtable = (throwentry *)sysRealloc(info->throwtable,
			sizeof(throwentry) * info->throwtablelen);
    info->throwtablesize = info->throwtablelen;
  }
#endif	// EXC_BY_SIGNAL

#ifdef COMPILE_DEBUG
  if (compile_debug) {
    printf("writeCode done%s: %s#%s %s.\n",
	((ret != 0) ? " (fail)" : ""),
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
    fflush(stdout);
  }
#endif

  return ret;
}


static void resolveJumpInstructions(CompilerContext *cc) {
  unsigned char *nativeCode = cc->buffer;

  jpentry *jptable = cc->jptable;
  jpentry *jptable_end = jptable + cc->jptablelen;
#ifdef COMPILE_DEBUG
  int compile_debug = cc->compile_debug;
#endif

  while (jptable < jptable_end) {
    unsigned int argoff = jptable->argoff;
    pcentry *tgttable = pctableGetByPC(cc, jptable->tgtoff);
    unsigned int tgtoff = tgttable->nativeoff;
    int32_t arg = tgtoff - (argoff + 4);

    unsigned char *p = nativeCode + argoff;
#ifdef COMPILE_DEBUG
    if (compile_debug) {
      printf("resolveJpInst(): arg. offset: 0x%x(%d)\n", argoff, argoff);
      printf("  target offset: byte 0x%x(%d), native 0x%x(%d)\n",
		jptable->tgtoff, jptable->tgtoff, tgtoff, tgtoff);
      fflush(stdout);
    }
#endif
    memcpy(p, &arg, 4);

#ifdef SHORTEN_JUMP_INSN
    // rewrite  `0xe9 XX XX XX XX' to `0xeb XX'
#  if 0
    if ((arg >= -132) && (arg <= 124))	// preliminary check
#  endif
    {
      char carg;
      p--;
      if (*p == 0xe9) {
	arg += 3;	// 5 byte -> 2 byte
	carg = (char)arg;

	if (arg == ((int32_t)carg)) {	// -128 <= (arg + 3) <= 127
	  p[0] = 0xeb;  p[1] = carg;	// 0xeb: jmp XX
	  p[4] = p[3] = p[2] = 0x90;	// 0x90: nop
	}
      }
      else {
	arg += 4;	// 6 byte -> 2 byte
	carg = (char)arg;

	if (arg == ((int32_t)carg)) {	// -128 <= (arg + 4) <= 127
	  p--;
	  p[0] = p[1] - 0x10;  p[1] = carg;	// jXX XX
	  *((int32_t *)(p + 2)) = 0x909002eb;	// 0x90: nop
	}
      }
    }
#endif	// SHORTEN_JUMP_INSN

    jptable++;
  }
}


/*
 * Set absolute/relative addresses into compiled code.
 */
static void resolveExcRetSwitch(CompilerContext *cc) {
  CodeInfo *info = (CodeInfo *)(cc->mb->CompiledCodeInfo);
  unsigned char *nativeCode = cc->buffer;

  pcentry *entry = cc->pctable;
  pcentry *entry_end = entry + pctableLen(cc);

  int i;
#ifdef COMPILE_DEBUG
  int compile_debug = cc->compile_debug;
#endif

  while (entry < entry_end) {
    int opcode = entry->opcode;
    int state = pcentryState(entry);
#ifdef COMPILE_DEBUG
    if (compile_debug) {
      int byteoff = entry->byteoff;
      int nativeoff = entry->nativeoff;

      printf("resERS(): %s(0x%02x,%d) st:%d\n",
	((opcode > opc_nonnull_quick)?"(null)":opcode_symbol[opcode]),
	opcode, opcode, state);
      printf("  off: b 0x%x(%d) n 0x%x(%d)\n",
	byteoff, byteoff, nativeoff, nativeoff);
      fflush(stdout);
    }
#endif

    // resolve PC in bytecode
    i = 0;
    if (bytepc_table[opcode][state][i] > 0) {
      uint32_t byteoff = entry->increasing_byteoff;
      while (bytepc_table[opcode][state][i] > 0) {
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  res. bytecode PC at 0x%x(%d) + %d: 0x%x(%d)\n",
		entry->nativeoff, entry->nativeoff,
		bytepc_table[opcode][state][i], byteoff, byteoff);
	  fflush(stdout);
	}
#endif
	memcpy(nativeCode + entry->nativeoff +
		bytepc_table[opcode][state][i], &byteoff, 4);
	i++;
      }
    }

    // resolve jump to exception handler
    i = 0;
    if (jumpexc_table[opcode][state][i] > 0) {
      unsigned int exc_off = info->exc_handler_nativeoff;
      unsigned int arg_off;
      int32_t relative_off;

      unsigned char *p;
      
      while (jumpexc_table[opcode][state][i] > 0) {
	arg_off = entry->nativeoff + jumpexc_table[opcode][state][i];
	relative_off = exc_off - (arg_off + 4);
	p = nativeCode + arg_off;
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  res. jump to exc.handler at 0x%x(%d) + %d: 0x%x(%d)\n",
		entry->nativeoff, entry->nativeoff,
		jumpexc_table[opcode][state][i], exc_off, exc_off);
	  fflush(stdout);
	}
#endif
	memcpy(p, &relative_off, 4);

#ifdef SHORTEN_JUMP_INSN
	// rewrite `0xe9 XX XX XX XX' to `0xeb XX'
	{
	  char carg;
	  if (*(--p) == 0xe9) {
	    relative_off += 3;  carg = (char)relative_off;

	    if (relative_off == ((int32_t)carg)) {	// -128 - 127
	       p[0] = 0xeb;  p[1] = carg;	// 0xeb: jmp XX
	       p[4] = p[3] = p[2] = 0x90;	// 0x90: nop
	    }
	  }
	}
#endif	// SHORTEN_JUMP_INSN

	i++;
      }
    }

    // resolve jump to finishing of method
    if (opcode == opc_return) {
      unsigned int fin_off = info->finish_return_nativeoff;
      unsigned int arg_off;
      int32_t relative_off;

      unsigned char *p;

      arg_off = entry->nativeoff + constant_table[opc_return][state][0];
      relative_off = fin_off - (arg_off + 4);
      p = nativeCode + arg_off;
#ifdef COMPILE_DEBUG
      if (compile_debug) {
	printf("  res. jump to fin.of method at 0x%x(%d) + %d: 0x%x(%d)\n",
		entry->nativeoff, entry->nativeoff,
		constant_table[opc_return][state][0], fin_off, fin_off);
	fflush(stdout);
      }
#endif
      memcpy(p, &relative_off, 4);

#ifdef SHORTEN_JUMP_INSN
      // rewrite `0xe9 XX XX XX XX' to `0xeb XX'
      {
	char carg;
	if (*(--p) == 0xe9) {
	  relative_off += 3;  carg = (char)relative_off;

	  if (relative_off == ((int32_t)carg)) {	// -128 - 127
	    p[0] = 0xeb;  p[1] = carg;	// 0xeb: jmp XX
	    p[4] = p[3] = p[2] = 0x90;	// 0x90: nop
	  }
	}
      }
#endif	// SHORTEN_JUMP_INSN
    }


    // make a table of native offset
    switch (opcode) {
    case opc_tableswitch:
      {
	unsigned int byteoff = entry->byteoff;
	pcentry *tgttable;
	CodeTable *codep;

	int32_t *argp = (int32_t *)ALIGNUP32(cc->mb->code + byteoff + 1);
	int32_t defoff = (int32_t)ntohl((uint32_t)argp[0]);
	int32_t l = (int32_t)ntohl((uint32_t)argp[1]);
	int32_t h = (int32_t)ntohl((uint32_t)argp[2]);
	int32_t *tblp;
	int last_state = code_table[opcode][state].last_state;
	int i;
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  low: %d, high: %d\n", l, h);  fflush(stdout);
	}
#endif

	cc->bufp = (unsigned char *)ALIGNUP32(cc->bufp);

	ensureBufferSize(cc, (h - l + 2) * 8);
	nativeCode = cc->buffer;

	tblp = (int32_t *)(cc->bufp);

	// set table offset into compiled code
	{
	  int32_t tbloff = ((unsigned char *)tblp) - nativeCode;
	  memcpy(nativeCode + entry->nativeoff
		+ constant_table[opcode][state][2], &tbloff, 4);

	  entry->operand = tbloff;
	}

	// default
	tgttable = pctableGetByPC(cc, byteoff + defoff);
	sysAssert(tgttable != NULL);
	tblp[0] = tgttable->nativeoff;
	codep = &code_table[opc_goto_st0 + pcentryState(tgttable)][last_state];
	tblp[1] = (int32_t)codep->offset;
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  unsigned int boff = byteoff + defoff;
	  printf("  default: byte 0x%x(%d), native 0x%x(%d)\n",
		boff, boff, tblp[0], tblp[0]);
	  fflush(stdout);
	}
#endif
	tblp += 2;
	argp += 3;	// skit default, low, and high

	for (i = 0; i < (h - l + 1); i++) {
	  int32_t off = (int32_t)ntohl((uint32_t)argp[i]);
	  tgttable = pctableGetByPC(cc, byteoff + off);
	  sysAssert(tgttable != NULL);
	  tblp[0] = tgttable->nativeoff;
	  codep =
		&code_table[opc_goto_st0 + pcentryState(tgttable)][last_state];
	  tblp[1] =
		(int32_t)codep->offset;
#ifdef COMPILE_DEBUG
	  if (compile_debug) {
	    unsigned int boff = byteoff + off;
	    printf("  %3d: offset: byte 0x%x(%d), native 0x%x(%d)\n",
			l + i, boff, boff, tblp[0], tblp[0]);
	    fflush(stdout);
	  }
#endif
	  tblp += 2;
	}

	cc->bufp = (unsigned char *)tblp;
      }
      break;
    case opc_lookupswitch:
      {
	unsigned int byteoff = entry->byteoff;
	pcentry *tgttable;
	CodeTable *codep;

	int32_t *argp = (int32_t *)ALIGNUP32(cc->mb->code + byteoff + 1);
	int32_t defoff = (int32_t)ntohl((uint32_t)argp[0]);
	int32_t npairs = (int32_t)ntohl((uint32_t)argp[1]);
	int32_t *tblp;
	int last_state = code_table[opcode][state].last_state;

#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  npairs: %d\n", npairs);  fflush(stdout);
	}
#endif

	cc->bufp = (unsigned char *)ALIGNUP32(cc->bufp);

	ensureBufferSize(cc, (npairs + 1) * 12);
	nativeCode = cc->buffer;

	tblp = (int32_t *)(cc->bufp);

	// set table offset int compiled code
	{
	  int32_t tbloff = ((unsigned char *)tblp) - nativeCode;
	  memcpy(nativeCode + entry->nativeoff
		+ constant_table[opcode][state][1], &tbloff, 4);

	  entry->operand = tbloff;
	}

	argp += 2;	// skip default and npairs

	// (int32_t)match and (int32_t)jump offset
	while (npairs-- > 0) {
	  int32_t match = (int32_t)ntohl((uint32_t)argp[0]);
	  int32_t off = (int32_t)ntohl((uint32_t)argp[1]);

	  tblp[0] = match;
	  tgttable = pctableGetByPC(cc, byteoff + off);
	  sysAssert(tgttable != NULL);
	  tblp[1] = tgttable->nativeoff;
	  codep =
		&code_table[opc_goto_st0 + pcentryState(tgttable)][last_state];
	  tblp[2] = (int32_t)codep->offset;
#ifdef COMPILE_DEBUG
	  if (compile_debug) {
	    unsigned int boff = byteoff + off;
	    printf("  match: %d, offset: byte 0x%x(%d), native 0x%x(%d)\n",
			match, boff, boff, tblp[1], tblp[1]);
	    fflush(stdout);
	  }
#endif
	  argp += 2;
	  tblp += 3;
	}

	// (int32_t)dummy_match and (int32_t)default
	tgttable = pctableGetByPC(cc, byteoff + defoff);
	sysAssert(tgttable != NULL);
	tblp[0] = 0;
	tblp[1] = tgttable->nativeoff;
	codep = &code_table[opc_goto_st0 + pcentryState(tgttable)][last_state];
	tblp[2] = (int32_t)codep->offset;
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  unsigned int boff = byteoff + defoff;
	  printf("  default offset: byte 0x%x(%d), native 0x%x(%d)\n",
			boff, boff, tblp[1], tblp[1]);
	  fflush(stdout);
	}
#endif
	tblp += 3;

	cc->bufp = (unsigned char *)tblp;
      }
      break;
    }

    entry++;
  }
}


static int resolveDynamicConstants(CompilerContext *cc) {
  struct methodblock *mb = cc->mb;
  cp_item_type *constant_pool = cbConstantPool(fieldclass(&mb->fb));
  unsigned char *type_table =
	constant_pool[CONSTANT_POOL_TYPE_TABLE_INDEX].type;

  pcentry *entry;
  int opcode, state;
  int32_t operand;
  int32_t nativeoff = 0;
  unsigned char *bytepc;
  int ret = 0;
  int i;

  unsigned char *bufp;
#ifdef METHOD_INLINING
  Stack *stack = newStack();
#endif
#ifdef COMPILE_DEBUG
  int compile_debug = cc->compile_debug;
#endif

  for (i = 0; i < pctableLen(cc); i++) {
    entry = cc->pctable + i;
    opcode = entry->opcode;
    operand = entry->operand;
    state = pcentryState(entry);
    bytepc = mb->code + entry->byteoff;
    nativeoff = entry->nativeoff;

#ifdef COMPILE_DEBUG
    if (compile_debug) {
      printf("resolveDynConst(): %s(0x%02x,%d) st:%d\n",
	((opcode > opc_nonnull_quick) ? "(null)" : opcode_symbol[opcode]),
	opcode, opcode, state);
      printf("  off: b 0x%x(%d) n 0x%x(%d)\n",
		entry->byteoff, entry->byteoff,
		nativeoff, nativeoff);
      printf("  oper: 0x%x(%d)\n", operand, operand);
      fflush(stdout);
    }
#endif

    // resolve dynamic constants
	// attention: var. nativeoff is already updated.
    bufp = cc->mb->CompiledCode + nativeoff;

    switch (opcode) {
    case opc_sync_static_enter:
    case opc_sync_static_exit:
      {
	ClassClass *cb = fieldclass(&mb->fb);
	memcpy(bufp + constant_table[opcode][state][0], &cb, 4);
      }
      break;

    case opc_bipush:
      switch (*bytepc) {
      case opc_ldc:
      case opc_ldc_w:
      case opc_ldc_quick:
      case opc_ldc_w_quick:
	sysAssert(operand != -1);
	if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand))
	  if (!ResolveClassConstantFromClass2(
		fieldclass(&mb->fb), operand, cc->ee,
		(1 << CONSTANT_Integer) | (1 << CONSTANT_Float) |
		(1 << CONSTANT_String), FALSE)) {
	    ret = 1;
	    goto resolvedyn_done;
	  }
	if (CONSTANT_POOL_TYPE_TABLE_GET_TYPE(type_table, operand) ==
		CONSTANT_String) {
	  int32_t val = constant_pool[operand].i;
#ifdef COMPILE_DEBUG
	  if (compile_debug) {
	    printf("  index: %d\n  value: 0x%x\n", operand, val);
	    fflush(stdout);
	  }
#endif
	  memcpy(bufp + constant_table[opcode][state][0], &val, 4);
	}
	break;
      }
      break;

    case opc_tableswitch:
      {
	int32_t *argp = (int32_t *)ALIGNUP32(bytepc + 1);
	int32_t l = (int32_t)ntohl((uint32_t)argp[1]);
	int32_t h = (int32_t)ntohl((uint32_t)argp[2]);

	int32_t *tblp = (int32_t *)(mb->CompiledCode + entry->operand);

	int j;

	for (j = 0; j < (h - l + 2); j++) {
	  tblp[1] += (int32_t)assembledCode;
	  tblp += 2;
	}
      }
      break;

    case opc_lookupswitch:
      {
	int32_t *argp = (int32_t *)ALIGNUP32(bytepc + 1);
	int32_t npairs = (int32_t)ntohl((uint32_t)argp[1]);

	int32_t *tblp = (int32_t *)(mb->CompiledCode + entry->operand);

	while (npairs-- > 0) {
	  tblp[2] += (int32_t)assembledCode;
	  tblp += 3;
	}
	tblp[2] += (int32_t)assembledCode;
      }
      break;

    case opc_getstatic:
    case opc_putstatic:
      {
	struct fieldblock *fb;
	OBJECT *addr;
	sysAssert(operand != -1);
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  index: %d\n", operand);  fflush(stdout);
	}
#endif
	if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {
	  if (!ResolveClassConstantFromClass2(
		fieldclass(&mb->fb), operand, cc->ee,
		1 << CONSTANT_Fieldref, FALSE)) {
	    ret = 1;
	    goto resolvedyn_done;
	  }
	}
	fb = constant_pool[operand].fb;
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  %s#%s\n", cbName(fb->clazz), fb->name);  fflush(stdout);
	}
#endif
	{
	  ClassClass *cb = fieldclass(fb);
#ifdef PATCH_WITH_SIGTRAP
#  define RC_STATIC_OFFSET 0
#elif defined(PATCH_ON_JUMP)
#  define RC_STATIC_OFFSET 1
	  memcpy(bufp + constant_table[opcode][state][0], &cb, 4);
#elif defined(INITCLASS_IN_COMPILATION)
#  define RC_STATIC_OFFSET 0
	  if (!CB_INITIALIZED(cb))  InitClass(cb);
#else
#  define RC_STATIC_OFFSET 0
#endif
	}
	addr = (OBJECT *)normal_static_address(fb);
	memcpy(bufp + constant_table[opcode][state][RC_STATIC_OFFSET],
		&addr, 4);
      }
      break;

    case opc_getstatic_quick:
    case opc_putstatic_quick:
      {
	struct fieldblock *fb;
	OBJECT *addr;
	sysAssert(operand != -1);

#ifdef CODEDB
	if (OPT_SETQ(OPT_CODEDB) &&
		!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {
	  if (!ResolveClassConstantFromClass2(
		fieldclass(&mb->fb), operand, cc->ee,
		1 << CONSTANT_Fieldref, FALSE)) {
	    ret = 1;
	    goto resolvedyn_done;
	  }
	}
#endif	// CODEDB
	fb = constant_pool[operand].fb;
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  %s#%s\n", cbName(fb->clazz), fb->name);  fflush(stdout);
	}
#endif
	addr = (OBJECT *)normal_static_address(fb);
	  memcpy(bufp + constant_table[opcode][state][0], &addr, 4);
      }
      break;

    case opc_getstatic2:
    case opc_putstatic2:
      {
	struct fieldblock *fb;
	stack_item *addr;
	sysAssert(operand != -1);
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  index: %d\n", operand);  fflush(stdout);
	}
#endif
	if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {
	  if (!ResolveClassConstantFromClass2(
		fieldclass(&mb->fb), operand, cc->ee,
		1 << CONSTANT_Fieldref, FALSE)) {
	    ret = 1;
	    goto resolvedyn_done;
	  }
	}
	fb = constant_pool[operand].fb;
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  %s#%s\n", cbName(fieldclass(fb)), fb->name);
	  fflush(stdout);
	}
#endif
	{
	  ClassClass *cb = fb->clazz;
#ifdef PATCH_WITH_SIGTRAP
#  define RC_STATIC2_OFFSET 0
#elif defined(PATCH_ON_JUMP)
#  define RC_STATIC2_OFFSET 1
	  memcpy(bufp + constant_table[opcode][state][0], &cb, 4);
#elif defined(INITCLASS_IN_COMPILATION)
#  define RC_STATIC2_OFFSET 0
	  if (!CB_INITIALIZED(cb))  InitClass(cb);
#else
#  define RC_STATIC2_OFFSET 0
#endif
	}
	addr = (stack_item *)twoword_static_address(fb);
	memcpy(bufp + constant_table[opcode][state][RC_STATIC2_OFFSET],
		&addr, 4);
      }
      break;

    case opc_getstatic2_quick:
    case opc_putstatic2_quick:
      {
	struct fieldblock *fb;
	stack_item *addr;
	sysAssert(operand != -1);

#ifdef CODEDB
	if (OPT_SETQ(OPT_CODEDB) &&
		!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {
	  if (!ResolveClassConstantFromClass2(
		fieldclass(&mb->fb), operand, cc->ee,
		1 << CONSTANT_Fieldref, FALSE)) {
	    ret = 1;
	    goto resolvedyn_done;
	  }
	}
#endif	// CODEDB
	fb = constant_pool[operand].fb;
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  %s#%s\n", cbName(fieldclass(fb)), fb->name);
	  fflush(stdout);
	}
#endif
	addr = (stack_item *)twoword_static_address(fb);
	memcpy(bufp + constant_table[opcode][state][0], &addr, 4);
      }
      break;

    case opc_invokevirtual:
    case opc_invokevirtual_obj:
    case opc_invokespecial:
    case opc_invokestatic_quick:
    case opc_invokestatic:
    case opc_invokeinterface:
      {
	struct methodblock *method;

	if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {
	  if (!ResolveClassConstantFromClass2(
		fieldclass(&mb->fb), operand, cc->ee,
		(1 << CONSTANT_InterfaceMethodref) | (1 << CONSTANT_Methodref),
		FALSE)) {
	    ret = 1;
	    goto resolvedyn_done;
	  }
	}

	method = constant_pool[operand].mb;

#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  %s#%s %s\n", cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature);
	  fflush(stdout);
	}
#endif

#if defined(INITCLASS_IN_COMPILATION) && !defined(PATCH_ON_JUMP) && !defined(PATCH_WITH_SIGTRAP)
	{
	  ClassClass *cb = fieldclass(&method->fb);
	  if (!CB_INITIALIZED(cb))  InitClass(cb);
	}
#endif

	switch (opcode) {
	case opc_invokespecial:
	case opc_invokestatic_quick:
	case opc_invokestatic:
	  memcpy(bufp + constant_table[opcode][state][0], &method, 4);
	  break;
	case opc_invokeinterface:
	  {
	    unsigned char *guessptr = bytepc + 4;
	    memcpy(bufp + constant_table[opcode][state][0], &guessptr, 4);
	    memcpy(bufp + constant_table[opcode][state][1], &method, 4);
	  }
	  break;
	}
      }
      break;

#ifdef COMPILE_DEBUG
#  define CONST_NEW_DEBUG1 \
      if (compile_debug) {\
	printf("  index: %d\n", operand); fflush(stdout);\
      }
#  define CONST_NEW_DEBUG2 \
      if (compile_debug) {\
	printf("  name: %s\n", cbName(cb)); fflush(stdout);\
      }
#else
#  define CONST_NEW_DEBUG1
#  define CONST_NEW_DEBUG2
#endif

#if defined(INITCLASS_IN_COMPILATION) && !defined(PATCH_ON_JUMP) && !defined(PATCH_WITH_SIGTRAP)
#  define CALL_INITCLASS_NEW \
	if (!CB_INITIALIZED(cb))  InitClass(cb);
#else
#  define CALL_INITCLASS_NEW
#endif

#define _CONST_NEW(vop, RESOLVE) \
	ClassClass *cb;\
	sysAssert(operand != -1);\
	CONST_NEW_DEBUG1;\
	\
	if (RESOLVE) {\
	  if (*bytepc == opc_##vop) {\
	    if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {\
	      if (!ResolveClassConstantFromClass2(fieldclass(&mb->fb),\
			operand, cc->ee, 1 << CONSTANT_Class, FALSE)) {\
		ret = 1;\
		goto resolvedyn_done;\
	      }\
	    }\
	  }\
	}\
	cb = constant_pool[operand].clazz;\
	CALL_INITCLASS_NEW;\
	CONST_NEW_DEBUG2;
#define CONST_NEW1(vop, RESOLVE) \
      {\
	_CONST_NEW(vop, RESOLVE);\
	memcpy(bufp + constant_table[opcode][state][0], &cb, 4);\
      }
#define CONST_NEW2(vop, RESOLVE) \
      {\
	_CONST_NEW(vop, RESOLVE);\
	memcpy(bufp + constant_table[opcode][state][0], &cb, 4);\
	memcpy(bufp + constant_table[opcode][state][1], &cb, 4);\
      }

    case opc_new:
      CONST_NEW1(new, 1);
      break;

    case opc_new_quick:
      CONST_NEW1(new, 1);	// in case OPT_CODEDB, RESOLVE cannot be 0.
      break;

    case opc_anewarray:
#if defined(METAVM) && !defined(METAVM_NO_ARRAY)
      CONST_NEW2(anewarray, 1);
#else
      CONST_NEW1(anewarray, 1);
#endif	// METAVM_NO_ARRAY
      break;

    case opc_checkcast:
      CONST_NEW1(checkcast, 1);
      break;

    case opc_instanceof:
      CONST_NEW1(instanceof, 1);
      break;

    case opc_multianewarray:
      {
	ClassClass *cb;
	sysAssert(operand != -1);
	if (*bytepc == opc_multianewarray) {
	  if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {
	    int res_result;
#if COMPILE_DEBUG
	    if (compile_debug) {
	      printf("  resolving cp[%d] type: %d.\n", operand,
		CONSTANT_POOL_TYPE_TABLE_GET_TYPE(type_table, operand));
	      fflush(stdout);
	    }
#endif
	    res_result = ResolveClassConstantFromClass2(
			fieldclass(&mb->fb), operand,
			cc->ee, 1 << CONSTANT_Class, FALSE);
#if COMPILE_DEBUG
	    if (compile_debug) {
	      printf("  resolving cp[%d] done.\n", operand);
	      if (exceptionOccurred(cc->ee)) {
		JHandle *exc = cc->ee->exception.exc;
		printf("exception occurred: %s\n",
			cbName(exc->methods->classdescriptor));
		showExcStackTrace(exc);
	      }
	      fflush(stdout);
	    }
#endif
	    if (!res_result) {
	      ret = 1;
	      goto resolvedyn_done;
	    }
	  }
	}
	cb = constant_pool[operand].clazz;
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  clazz: %s\n", cbName(cb));  fflush(stdout);
	}
#endif
	memcpy(bufp + constant_table[opcode][state][1], &cb, 4);
#if defined(METAVM) && !defined(METAVM_NO_ARRAY)
	memcpy(bufp + constant_table[opcode][state][2], &cb, 4);
#endif	// METAVM_NO_ARRAY
      }
      break;

#if (defined(INITCLASS_IN_COMPILATION) && !defined(PATCH_ON_JUMP)) || defined(PATCH_WITH_SIGTRAP)
#else
    case opc_invokeignored_static:
    // invokeignored_static_quick doesn't need this process
      {
	ClassClass *cb;
	sysAssert(operand != -1);

#ifdef CODEDB
	if (OPT_SETQ(OPT_CODEDB) &&
		!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {
	  if (!ResolveClassConstantFromClass2(fieldclass(&mb->fb),
		operand, cc->ee, 1 << CONSTANT_Methodref, FALSE)) {
	    ret = 1;
	    goto resolvedyn_done;
	  }
	}
#endif	// CODEDB

	cb = fieldclass(&(constant_pool[operand].mb->fb));

	memcpy(bufp + constant_table[opcode][state][0], &cb, 4);
      }
      break;
#endif

#ifdef METHOD_INLINING
    case opc_inlined_enter:
      // maintain methodblock
      pushToStack(stack, (long)mb);
      if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, operand)) {
	if (!ResolveClassConstantFromClass2(
		fieldclass(&mb->fb), operand, cc->ee,
		(1 << CONSTANT_InterfaceMethodref) | (1 << CONSTANT_Methodref),
		FALSE)) {
	  ret = 1;
	  goto resolvedyn_done;
	}
      }
      mb = constant_pool[operand].mb;
      constant_pool = cbConstantPool(fieldclass(&mb->fb));
      type_table = constant_pool[CONSTANT_POOL_TYPE_TABLE_INDEX].type;
      break;

    case opc_inlined_exit:
      // maintain methodblock
      mb = (struct methodblock *)popFromStack(stack);
      constant_pool = cbConstantPool(fieldclass(&mb->fb));
      type_table = constant_pool[CONSTANT_POOL_TYPE_TABLE_INDEX].type;
      break;

#  if defined(PATCH_ON_JUMP) && !defined(PATCH_WITH_SIGTRAP) && !defined(INITCLASS_IN_COMPILATION)
    case opc_init_class:
      {
	ClassClass *cb = fieldclass(&mb->fb);	// mb has been set
	memcpy(bufp + constant_table[opcode][state][0], &cb, 4);
      }
      break;
#  endif
#endif	// METHOD_INLINING
    }	// switch (opcode)
  }	// for (i = 0; i < pctableLen(cc); i++)


resolvedyn_done:

#ifdef METHOD_INLINING
  freeStack(stack);
#endif

#ifdef COMPILE_DEBUG
  if (compile_debug) {
    printf("resolveDynConst() done.\n");
    fflush(stdout);
  }
#endif

  return ret;
}


static void makeExcTable(CompilerContext *cc) {
  struct CatchFrame_w_state *cf;
  int i;
#ifdef COMPILE_DEBUG
  int compile_debug = cc->compile_debug;
#endif

#ifdef COMPILE_DEBUG
  if (compile_debug) {
    printf("makeExcTable() called.\n");
    fflush(stdout);
  }
#endif

  cf = (CatchFrame_w_state *)cc->mb->exception_table;
  for (i = 0; i < cc->mb->exception_table_length; i++) {
    pcentry *table = pctableGetByPC(cc, cf->handler_pc);
    int nativeoff = table->nativeoff;
		// offset in byte code -> offset in native code
    cf->compiled_CatchFrame = (void *)nativeoff;
    cf->state = pcentryState(table);

    cf++;
  }
#ifdef COMPILE_DEBUG
  if (compile_debug) {
    printf("makeExcTable() done.\n");
    fflush(stdout);
  }
#endif
}


#undef RES_FUNC_DEBUG
static void resolveFunctionSymbols(CompilerContext *cc) {
  unsigned char *nativeCode = cc->mb->CompiledCode;

  pcentry *entry = cc->pctable;
  pcentry *entry_end = entry + pctableLen(cc);

  FuncTable *funcp;
#ifdef RES_FUNC_DEBUG
  printf("resolveFuncSym() called.\n");  fflush(stdout);
#endif

  while (entry < entry_end) {
    int opcode = entry->opcode;
    int state = pcentryState(entry);

    funcp = func_table[opcode][state];

    while (funcp->offset >= 0) {
      unsigned char *argoff;
      unsigned char *funcptr;
      int32_t arg;

      funcptr = funcp->address;
      if (!funcptr) {
	/* NOTREACHED */
	fprintf(stderr, "FATAL: symbol `%s' was not resolved.\n",
			funcp->address);
	JVM_Exit(1);
      }

#ifdef RES_FUNC_DEBUG
      printf("  symb %3d:%d: 0x%08x at 0x%x(%d) + 0x%x\n",
		opcode, state, (int)funcptr,
		entry->nativeoff, entry->nativeoff, funcp->offset);
      fflush(stdout);
#endif
      argoff = nativeCode + entry->nativeoff + funcp->offset;
      arg = funcptr - (argoff + 4);
      memcpy(argoff, &arg, 4);

      funcp++;
    }

    entry++;
  }
#ifdef RES_FUNC_DEBUG
  printf("resolveFuncSym() done.\n");  fflush(stdout);
#endif
}
