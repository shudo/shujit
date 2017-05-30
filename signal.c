/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1998,1999,2000,2001,2002,2005 Kazuyuki Shudo

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

#include "compiler.h"

#if defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)
#  include <signal.h>
#  if defined(linux) && defined(HAVE_UCONTEXT_H)
#    if (__GLIBC__ == 2) && (__GLIBC_MINOR__ == 0)
typedef struct sigaltstack stack_t;
	// glibc 2.0 needs stack_t type. but cannot include <asm/signal.h>
#    endif	// glibc 2.0
#    include <asm/ucontext.h>	// neither <ucontext.h> nor <sys/ucontext.h>
#  endif
#endif	// EXC_BY_SIGNAL || GET_SIGCONTEXT


//
// Local Functions
//
static void showFPUEnv(FILE *, char *indent, char fpuenv[28]);
#define fillFPUEnv(FPUENV)	asm("fstenv %0" : "=m" (FPUENV))
#if defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)
static void showSigcontext(SIGCONTEXT_T *sc);
#endif	// EXC_BY_SIGNAL || GET_SIGCONTEXT


//
// Definitions
//

// access to sigcontext
#ifdef linux
#  define SIGCONTEXT(SC, REGNAME)	((SC)->REGNAME)
#elif defined(__FreeBSD__) || defined(__NetBSD__)
#  define SIGCONTEXT(SC, REGNAME)	((SC)->sc_##REGNAME)
#elif defined(sun)
#  define SIGCONTEXT(SC, REGNAME)	((SC).gregs[REG_##REGNAME])
#else
#  define SIGCONTEXT(SC, REGNAME)	((SC)->REGNAME)
#endif

// return from signal handler
// JDK 1.2: set program counter to jump to exception handler
// JDK 1.1: jump to exception handler */
#if JDK_VER >= 12
#  define SIGRETURN(SC, EBP)	return TRUE;
#else
#  ifdef linux
#    define SIGRETURN(SC, EBP) \
  do {\
    register int eax asm("eax");\
    asm("movl  %1,%%ebp" : "=a" (eax) : "m" (EBP));\
    return eax;\
  } while (0)
#  elif defined(__FreeBSD__) || defined(__NetBSD__)
#    define SIGRETURN(SC, EBP)	sigreturn(SC)
#  elif defined(sun)
#    define SIGRETURN(SC, EBP)	sigcontext(SC)
#  else
#    define SIGRETURN(SC, EBP)  sigreturn(SC)
#  endif	// os
#endif	// JDK_VER



//
// Variables
//
#if (defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)) && defined(SEARCH_SIGCONTEXT)
int sc_nest = -1;	// -1: invalid
static int ucontext_used = 0;
#endif

#if defined(EXC_BY_SIGNAL) && defined(COUNT_EXC_SIGNAL)
static int exc_signal_count = 0;
#endif



#if (defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)) && defined(SEARCH_SIGCONTEXT)
bool_t examineSigcontextNestCount(int sig, void *info, void *uc0) {
  SIGCONTEXT_T *sc, *found_sc;
  uint32_t *ebp, *found_ebp = 0;
  int i;
  extern void exam_nest(void);	// in compiler.c

#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG)
  printf("examineSigcontextNestCount: sig %d (sys_thread_t: %x)\n",
	sig, (int)sysThreadSelf());
  printf("  exam_next: 0x%x\n", (uint32_t)exam_nest);
#endif

  asm("movl  %%ebp,%0" : "=r" (ebp));

#define SC_SEARCH_WIDTH	10
  for (i = 0; i < SC_SEARCH_WIDTH; i++) {
#ifdef RUNTIME_DEBUG
    printf("  i: %d, ebp: %x", i, ebp);
    fflush(stdout);
#endif
    sc = (SIGCONTEXT_T *)(ebp + 3);
#ifdef RUNTIME_DEBUG
    printf(", eip:%x\n", SIGCONTEXT(sc, eip));
    fflush(stdout);
#endif
    if (SIGCONTEXT(sc, eip) == ((uint32_t)exam_nest)) {	// found
      sc_nest = i;
      ucontext_used = 0;	// false
      found_sc = sc;
      found_ebp = ebp;
#ifdef RUNTIME_DEBUG
      printf("  sc_nest: %d, ucontext is not used.\n", sc_nest);
      fflush(stdout);
#endif
      // break;
		// must not do break to use the last found sigcontext
		// with LinuxThreads in glibc 2.1.2
    }
#ifdef HAVE_UCONTEXT_H
    else {	// not found
      struct ucontext *uc;
      uc = (struct ucontext *)ebp[4];
      if ((ebp < (uint32_t *)uc) && ((uint32_t *)uc < ebp + 0x100)) {
	sc = (SIGCONTEXT_T *)&uc->uc_mcontext;
	if (SIGCONTEXT(sc, eip) == ((uint32_t)exam_nest)) {	// found
	  sc_nest = i;
	  ucontext_used = 1;	// true
	  found_sc = sc;
	  found_ebp = ebp;
#ifdef RUNTIME_DEBUG
	  printf("  sc_nest: %d, ucontext is used.\n", sc_nest);
	  fflush(stdout);
#endif
	  break;
	}
      }
    }
#endif	// HAVE_UCONTEXT_H

    ebp = (uint32_t *)ebp[0];
  }

  if (sc_nest < 0) {
    printf("FATAL: cannot examine the offset of signal context.\n");
    JVM_Exit(1);
  }

#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG)
  printf("sigcontext: 0x%08x\n", (int)found_sc);  fflush(stdout);
  showSigcontext(found_sc);
#endif

  SIGRETURN(found_sc, found_ebp);
}
#endif	// (EXC_BY_SIGNAL || GET_SIGCONTEXT) && SEARCH_SIGCONTEXT


bool_t signalHandler(int sig, void *info, void *uc0) {
#if defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)
#  ifdef SEARCH_SIGCONTEXT
  uint32_t *ebp;
#  endif	// SEARCH_SIGCONTEXT
  ExecEnv *ee = EE();
  SIGCONTEXT_T *sc = NULL;
  struct methodblock *mb;
  CodeInfo *codeinfo;
  uint32_t native_off;
#endif	// EXC_BY_SIGNAL || GET_SIGCONTEXT
#ifdef EXC_BY_SIGNAL
  throwentry *tentry;
  int opcode;
  uint32_t jump_target = 0;
#endif	// EXC_BY_SIGNAL
  int i;

#if 0
#if 1
  signal(sig, SIG_DFL);
#else
  {
    struct sigaction sigAct;
    sigAct.sa_handler = SIG_DFL;
    sigemptyset(&sigAct.sa_mask);
    sigaction(sig, &sigAct, (struct sigaction *)NULL);
  }
#endif
#endif	// 0

#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG)
  printf("Signal handler: sig %d (sys_thread_t: %x)\n",
	sig, (int)sysThreadSelf());
#endif


  //
  // obtain sigcontext
  //
#if defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)
#ifndef SEARCH_SIGCONTEXT
#  if defined(__FreeBSD__) || defined(__NetBSD__)
  sc = (SIGCONTEXT_T *)uc0;
#  else	// linux
  sc = (SIGCONTEXT_T *) &((struct ucontext *)uc0)->uc_mcontext;
#  endif
#else
  // get signal context
  asm("movl  %%ebp,%0" : "=r" (ebp));
  for (i = 0; i < sc_nest; i++) {
    ebp = (uint32_t *)ebp[0];
  }
  if (!ucontext_used) {
    sc = (SIGCONTEXT_T *)(ebp + 3);
  }
#ifdef HAVE_UCONTEXT_H
  else {
    sc = (SIGCONTEXT_T *) &((struct ucontext *)ebp[4])->uc_mcontext;
  }
#endif	// HAVE_UCONTEXT_H
#endif	// SEARCH_SIGCONTEXT

#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG) || (defined(GET_SIGCONTEXT) && !defined(EXC_BY_SIGNAL))
  printf("sigcontext: 0x%08x\n", (int)sc);  fflush(stdout);
  showSigcontext(sc);
#endif
#endif	// EXC_BY_SIGNAL || GET_SIGCONTEXT


#if defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)
  if ((sig != SIGSEGV) && (sig != SIGFPE) && (sig != SIGTRAP))
    return FALSE;

  if ((sig != SIGFPE) &&  // handle SIGFPE in __divdi3() and __moddi3()
      (((unsigned char *)SIGCONTEXT(sc, eip) < compiledcode_min) ||
       (compiledcode_max < (unsigned char *)SIGCONTEXT(sc, eip)))) {

    printf("FATAL: Signal %d occurred out of JIT compiled code.\n", sig);
    showSigcontext(sc);
    fflush(stdout);

    return FALSE;
  }

  //
  // calculate offset in compiled code
  //
  mb = ee->current_frame->current_method;
  if (!mb)  return FALSE;
#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG)
  printf("method(0x%x)", (int)mb);  fflush(stdout);
  printf(": %s#%s %s\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
  fflush(stdout);
#endif
  if (!mb->CompiledCode)  return FALSE;		// not compiled
  native_off =
	((uint32_t)SIGCONTEXT(sc, eip)) - ((uint32_t)mb->CompiledCode);
#ifdef RUNTIME_DEBUG
  printf("native off: 0x%x\n", native_off);
  fflush(stdout);
#endif
#ifndef EXC_BY_SIGNAL
  return FALSE;
#else


  //
  // obtain (throwentry *)tentry
  //
  codeinfo = (CodeInfo *)(mb->CompiledCodeInfo);
  tentry = throwtableGet(codeinfo, native_off);

  if (tentry) {
    opcode = *(mb->code + tentry->byteoff);
	// not internal opcode
  }
  else if (!tentry) {	// search EIP in native code
    uint32_t *ebp;
#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG)
    printf("throwentry is null !\n");  fflush(stdout);
#endif	// RUNTIME_DEBUG || COMPILE_DEBUG

    ebp = (uint32_t *)SIGCONTEXT(sc, ebp);

#define IP_SEARCH_COUNT	3
    for (i = 0; i < IP_SEARCH_COUNT; i++) {
      uint32_t *sp = ebp + 1;

      native_off = *sp - (uint32_t)mb->CompiledCode;
      if ((native_off >= 0) && (native_off <= codeinfo->code_size)) {
	tentry = throwtableGet(codeinfo, native_off);
	if (tentry) {
#ifdef ARITHEXC_BY_SIGNAL
	  opcode = *(mb->code + tentry->byteoff);
	  if (sig == SIGFPE) {
	    if ((opcode != opc_idiv) && (opcode != opc_irem) &&
		(opcode != opc_ldiv) && (opcode != opc_lrem))
	      continue;
	  }
#endif	// ARITHEXC_BY_SIGNAL
	  SIGCONTEXT(sc, esp) = (uint32_t)(sp + 1);
					// stack top in the frame
	  SIGCONTEXT(sc, eip) = *sp;	// %eip on the compiled code
	  SIGCONTEXT(sc, ebp) = ebp[0];	// return one more frame
	  goto tentry_is_found;
	}
      }
      ebp = (uint32_t *)ebp[0];
    }	// for (i = 0; i < IP_SEARCH_COUNT; ...
    goto signal_handler_error;
  }
  tentry_is_found:
#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG)
  printf("throwentry: ");
  fflush(stdout);
  if (tentry)
    printf("start: 0x%x, len: 0x%x, byteoff: %d(0x%x), opcode: %d(0x%x)\n",
	tentry->start, tentry->len, tentry->byteoff, tentry->byteoff,
	opcode, opcode);
  else
    printf("(null)\n");
  fflush(stdout);
#endif	// RUNTIME_DEBUG || COMPILE_DEBUG


  //
  // manage the observed signal
  //
  switch (sig) {
#ifdef NULLEXC_BY_SIGNAL
  case SIGSEGV:
    if (!exceptionOccurred(ee)) {
#  ifdef CAUSE_STACKOVERFLOW
      unsigned char *pc = (unsigned char *)SIGCONTEXT(sc, eip);
      if ((pc[0] == 0x8b) && (pc[1] == 0x8d) &&
	  ((pc[2] + (pc[3] << 8) + (pc[4] << 16) + (pc[5] << 24))
		== -STACKOVERFLOW_MARGIN)) {
		// movl -STACKOVERFLOW_MARGIN(%ebp),%ecx
	SignalError(NULL, JAVAPKG "StackOverflowError", 0);
      }
      else
#  endif	// CAUSE_STACKOVERFLOW
      {
	SignalError(NULL, JAVAPKG "NullPointerException", 0);
#ifdef COUNT_EXC_SIGNAL
	exc_signal_count++;
#endif
      }
    }
    break;
#endif	// NULLEXC_BY_SIGNAL

#ifdef ARITHEXC_BY_SIGNAL
  case SIGFPE:
    if ((opcode == opc_idiv) || (opcode == opc_irem)) {
      if (SIGCONTEXT(sc, ecx) /* divisor */ == 0) {
	// ArithmeticException occurred
	SignalError(NULL, JAVAPKG "ArithmeticException", "/ by zero");
#ifdef COUNT_EXC_SIGNAL
	exc_signal_count++;
#endif
      }
      else if ((SIGCONTEXT(sc, eax) /* dividend */ == 0x80000000) &&
	       (SIGCONTEXT(sc, ecx) /* divisor  */ == -1)) {
	// idiv insn. of x86 causes it: 0x80000000 / -1
	SIGCONTEXT(sc, eax) = 0x80000000;
	SIGCONTEXT(sc, edx) = 0;
	jump_target = SIGCONTEXT(sc, eip) + 2;	// `+ 2' skips idiv `0xf7 0xXX'
      }
      else {
	float dividend, divisor;
	memcpy(&dividend, &SIGCONTEXT(sc, eax), 4);
	memcpy(&divisor, &SIGCONTEXT(sc, ecx), 4);
	printf("FATAL: SIGFPE occurred in %s: %e %s %e.\n",
		((opcode == opc_idiv)?"idiv":"irem"),
		(double)dividend, ((opcode == opc_idiv)?"/":"%"),
		(double)divisor);
	fflush(stdout);

	goto signal_handler_error;
      }
    }
    else if ((opcode == opc_ldiv) || (opcode == opc_lrem)) {
      uint32_t *sp = (uint32_t *)SIGCONTEXT(sc, esp);

      // `back up'ed registers in ldiv
      SIGCONTEXT(sc, ebp) = *(sp + 4);	// *(sp + 4) is %ebp
      SIGCONTEXT(sc, ebx) = *(sp + 5);	// *(sp + 5) is %ebx
      SIGCONTEXT(sc, esi) = *(sp + 6);	// *(sp + 6) is %esi

      SIGCONTEXT(sc, esp) += 28;
				// clear arguments of __divdi3, __moddi3

      // dividend: (*(sp + 1) << 32) | *sp
      // divisor : (*(sp + 3) << 32) | *(sp + 2)

      if (!((*(sp + 2)) | (*(sp + 3)))) {
	// ArithmeticException occurred
	SignalError(NULL, JAVAPKG "ArithmeticException", "/ by zero");
#ifdef COUNT_EXC_SIGNAL
	exc_signal_count++;
#endif
      }
#if 0
      // In this case,
      // functions called by ldiv (__divdi3(),__moddi3()) don't cause SIGFPE
      else if ((*sp == 0) && (*(sp + 1) == 0x80000000) &&
	       ((*(sp +2) & *(sp + 3)) == 0xffffffff)) {
      }
#endif
    }
    else {
      printf("FATAL: SIGFPE occured in %s.",
	((opcode == opc_ldiv)?"ldiv":"lrem"));
      fflush(stdout);

      goto signal_handler_error;
    }
    break;
#endif	// ARITHEXC_BY_SIGNAL

#ifdef PATCH_WITH_SIGTRAP
  case SIGTRAP:
    {
      cp_item_type *constant_pool = cbConstantPool(fieldclass(&mb->fb));
      uint8_t *trampoline = codeinfo->trampoline;
      uint8_t *once_function = NULL;

      switch (tentry->opcode) {
		// not an opcode variable.
		// tentry->opcode is the exact internal opcode even if inlined.
      case opc_getstatic:  case opc_getstatic2:
      case opc_putstatic:  case opc_putstatic2:
#  ifdef METHOD_INLINING
      case opc_inlined_enter:
#  endif
	{
	  ClassClass *cb = tentry->cb;
	  sysAssert(cb != NULL);
#ifdef RUNTIME_DEBUG
	  printf("once_InitClass(%s)\n", cbName(cb));
	  fflush(stdout);
#endif

	  // arguments for once_InitClass();
	  *((uint32_t *)(trampoline + 13)) = (uint32_t)cb;
	  once_function = (uint8_t *)once_InitClass;
	}
	break;

      case opc_invokestatic:
      case opc_invokeignored_static:
	{
	  ClassClass *cb = tentry->cb;
	  sysAssert(cb != NULL);
#ifdef RUNTIME_DEBUG
	  printf("once_InitClass(%s)\n", cbName(cb));
	  fflush(stdout);
#endif

	  // arguments for once_InitClass();
	  *((uint32_t *)(trampoline + 13)) = (uint32_t)cb;
	  once_function = (uint8_t *)once_InitClass;
	}
	break;

      case opc_invokeinterface:
	SIGCONTEXT(sc, eip)--;
		// adjust PC
	*((uint8_t *)SIGCONTEXT(sc, eip)) = tentry->patched_code;
		// patch

	if (SIGCONTEXT(sc, eax) != 0) {
	  SIGRETURN(sc, ebp);
	}
	else
	  goto sigtrap_exc;

	// NOT REACHED
	break;

      case opc_new:
	{
	  ClassClass *cb, *cur_cb;
	  cb = tentry->cb;
	  cur_cb = fieldclass(&mb->fb);
#ifdef RUNTIME_DEBUG
	  printf("once_InitClass(%s)\n", cbName(cb));
	  fflush(stdout);
#endif

	  // arguments for once_InitClass();
	  *((uint32_t *)(trampoline + 13)) = (uint32_t)cb;
	  once_function = (uint8_t *)once_InitClass;
	}
	break;

      default:
	return FALSE;
      }

      // make a trampoline and jump to it
      {
	uint8_t *target_pc;

	// ExecEnv
	*((uint32_t *)(trampoline + 18)) = (uint32_t)ee;

	// original PC
	target_pc = ((uint8_t *)SIGCONTEXT(sc, eip)) - 1;
	*target_pc = tentry->patched_code;	// patch
	*((uint32_t *)(trampoline + 3)) = (uint32_t)target_pc;

	// exc handler
	target_pc = mb->CompiledCode + codeinfo->exc_handler_nativeoff;
#if 1
	*((uint32_t *)(trampoline + 37)) =
		(uint32_t)(target_pc - (trampoline + 41));
#else
	*((uint32_t *)(trampoline + 37)) =
		(uint32_t)0;
#endif

	// function once_*()
	*((uint32_t *)(trampoline + 23)) =
		(uint32_t)(once_function - (trampoline + 27));

	SIGCONTEXT(sc, eip) = (uint32_t)trampoline;

	SIGRETURN(sc, ebp);
      }
    }

  sigtrap_exc:
    break;
#endif	// PATCH_WITH_SIGTRAP

  default:
    return FALSE;
  }


#ifdef RUNTIME_DEBUG
  // examine whether the signal is pending
  {
    sigset_t pending_set;
    if ((sigpending(&pending_set)) < 0)  perror("sigpending");
    else {
      printf("signal %d is pending: %s\n",
	sig, (sigismember(&pending_set, sig) ? "TRUE":"FALSE"));
      fflush(stdout);
    }
  }
#endif


  //
  // jump to exc_new or exc_handler in compiled code
  //
#if 0
  // re-initialize signal handler
  // associated with raised signal
  {
    struct sigaction sigAct = sigActForHandler;
    sigaction(sig, &sigAct, (struct sigaction *)NULL);
  }

#if defined(__FreeBSD__) || defined(__NetBSD__)
  // unmask the signal
  {
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, sig);
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
  }
#endif	// __FreeBSD__ || __NetBSD__
#endif	// 0


  // set (int32_t)bytepcoff (local variable in assembledCode())
  *(((int32_t *)SIGCONTEXT(sc, ebp)) - 1) = tentry->byteoff;


  {
    uint32_t exc_handler;
    exc_handler = jump_target ? jump_target :
		(uint32_t)(mb->CompiledCode + codeinfo->exc_handler_nativeoff);
#ifdef RUNTIME_DEBUG
    printf("jump to 0x%x\n", exc_handler);  fflush(stdout);
#endif
    SIGCONTEXT(sc, eip) = exc_handler;
  }

  SIGRETURN(sc, ebp);
#endif	// EXC_BY_SIGNAL
#endif	// EXC_BY_SIGNAL || GET_SIGCONTEXT

  return FALSE;

signal_handler_error:
  fprintf(stderr, "Signal %d handling failed\n", sig);
  fprintf(stderr, "shuJIT " VERSION " compiled with " GCC_VER_STRING "\n");
  fflush(stderr);
#if defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)
  if (sc) {
    struct methodblock *sig_mb =
	methodByPC((unsigned char *)SIGCONTEXT(sc, eip));
    if (sig_mb) {
      uint32_t nativepc =
		(uint32_t)SIGCONTEXT(sc, eip) - (uint32_t)sig_mb->CompiledCode;
      CodeInfo *info = (CodeInfo *)sig_mb->CompiledCodeInfo;
      fprintf(stderr, "  in ");
      fprintf(stderr, "%s#%s %s\n",
		cbName(fieldclass(&sig_mb->fb)),
		sig_mb->fb.name, sig_mb->fb.signature);
      fprintf(stderr, "  native PC: 0x%x(%d)\n", nativepc, nativepc);
#ifdef EXC_BY_SIGNAL
      if (info) {
	throwentry *tentry = throwtableGet(info, nativepc);
	if (tentry) {
	  int bytepc = tentry->byteoff;
	  fprintf(stderr, "  byte   PC: 0x%x(%d)\n", bytepc, bytepc);
	}
      }
#endif	// EXC_BY_SIGNAL
    }
    fprintf(stderr, "\n");
    {
      char fpuenv[28];
      fillFPUEnv(fpuenv);
      showFPUEnv(stderr, NULL, fpuenv);
    }
    fprintf(stderr, "\n");
    fflush(stderr);

    showSigcontext(sc);

    fprintf(stderr, "\n");
    fflush(stderr);
  }
#endif
  JVM_Exit(1);
}


static void showFPUEnv(FILE *fp, char *indent, char fpuenv[28]) {
  if (!indent)  indent = "";
  if (!fp)  fp = stdout;

  fprintf(fp, "%sFPU control word: 0x%04x\n", indent,
		*((int16_t *)(fpuenv + 0)));
  fprintf(fp, "%sFPU status  word: 0x%04x\n", indent,
		*((int16_t *)(fpuenv + 4)));
  fprintf(fp, "%sFPU tag     word: 0x%04x\n", indent,
		*((int16_t *)(fpuenv + 8)));
  fprintf(fp, "%sFPU FIP         : 0x%08x\n", indent,
		*((int32_t *)(fpuenv + 12)));

  fflush(fp);
}


#if defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)
/*
 * show contents of structure sigcontext
 */
static void showSigcontext(SIGCONTEXT_T *sc) {
  printf("SS: %04x, CS: %04x, DS: %04x, ES: %04x, FS: %04x, GS: %04x\n",
#if defined(__FreeBSD__) || defined(__NetBSD__)
	SIGCONTEXT(sc, ss) & 0xffff, SIGCONTEXT(sc, cs) & 0xffff,
	SIGCONTEXT(sc, ds) & 0xffff, SIGCONTEXT(sc, es) & 0xffff,
	SIGCONTEXT(sc, fs) & 0xffff, SIGCONTEXT(sc, gs) & 0xffff
#else
	SIGCONTEXT(sc, ss), SIGCONTEXT(sc, cs),
	SIGCONTEXT(sc, ds), SIGCONTEXT(sc, es),
	SIGCONTEXT(sc, fs), SIGCONTEXT(sc, gs)
#endif
  );

  printf("EAX: %08x, ECX: %08x, EDX: %08x, EBX: %08x\n",
	SIGCONTEXT(sc, eax), SIGCONTEXT(sc, ecx),
	SIGCONTEXT(sc, edx), SIGCONTEXT(sc, ebx));

  printf("ESI: %08x, EDI: %08x\n",
	SIGCONTEXT(sc, esi), SIGCONTEXT(sc, edi));

  printf("ESP: %08x, EBP: %08x EIP: %08x\n",
	SIGCONTEXT(sc, esp), SIGCONTEXT(sc, ebp), SIGCONTEXT(sc, eip));

#if defined(linux)
  printf("ESP at signal: %08x\n", sc->esp_at_signal);
#endif

  {
    uint32_t *esp = (uint32_t *)SIGCONTEXT(sc, esp);
    uint32_t *ebp = (uint32_t *)SIGCONTEXT(sc, ebp);
    if (esp)
      printf("(ESP+4): %08x, (ESP): %08x\n", *(esp+1), *esp);
    if (ebp)
      printf("(EBP+4): %08x (retrun addr.)\n", *(ebp+1));
  }
  {
    unsigned char *eip = (unsigned char *)SIGCONTEXT(sc, eip);
    if (eip) {
      int i;

      if (*(eip-1) == 0xcc) {
	// INT $3
	eip--;
	printf("(EIP-1):");
      }
      else if ((*(eip-2) == 0xcd) && (*(eip-1) == 0x80)) {
	// INT $XX
	eip -= 2;
	printf("(EIP-2):");
      }
      else
	printf("(EIP):");

      for (i = 0; i < 8; i++)  printf(" %02x", eip[i]);
      printf(" ");
      for (; i < 16; i++)  printf(" %02x", eip[i]);
      printf("\n");
    }
  }

  printf("trapno: %02x\n", SIGCONTEXT(sc, trapno));
  fflush(stdout);

  // search the method where the signal thrown
  {
    uint32_t off = (uint32_t)SIGCONTEXT(sc, eip);
    struct methodblock *mb_in_frame =
	*(struct methodblock **)((unsigned char *)SIGCONTEXT(sc, ebp) + 12);
    ClassClass **classes, **clazzptr;
    struct methodblock *mb, *mb_end;
    int i;

    classes = (ClassClass **)sysMalloc(sizeof(ClassClass *) * nbinclasses);
    memcpy(classes, binclasses, sizeof(ClassClass *) * nbinclasses);

    for (i = nbinclasses, clazzptr = classes; --i >= 0; clazzptr++) {
      mb = cbMethods(*clazzptr);
      mb_end = mb + cbMethodsCount(*clazzptr);
      for (; mb < mb_end; mb++) {
	//int access = mb->fb.access;
	//if (access & (ACC_ABSTRACT | ACC_NATIVE))  continue;

	if (mb == mb_in_frame) {
	  printf("method by EIP (at EBP + 12)");
	  goto mb_found;
	}

	if (mb->invoker != sym_invokeJITCompiledMethod)  continue;

	if ((off - (uint32_t)mb->CompiledCode) <
	    ((CodeInfo *)(mb->CompiledCodeInfo))->code_size) {
	  printf("method by EIP (in native code)");
	  goto mb_found;
	}
      }
    }
    mb = NULL;

  mb_found:
    if (mb != NULL) {
      printf(": %s#%s %s\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
    }
    else {
      printf("method not found by EIP.\n");
    }
    fflush(stdout);

    sysFree(classes);
  }
}
#endif	// EXC_BY_SIGNAL || GET_SIGCONTEXT


#if defined(EXC_BY_SIGNAL) && defined(COUNT_EXC_SIGNAL)
void showExcSignalCount() {
  printf("# of exceptions by signal: %d\n", exc_signal_count);
  fflush(stdout);
}
#endif	// EXC_BY_SIGNAL && COUNT_EXC_SIGNAL
