/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1998,1999,2000,2001,2002,2003 Kazuyuki Shudo

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

#include <string.h>	// for strcmp()
#include <stdlib.h>	// for alloca()
#ifdef _WIN32
#  include <malloc.h>
#endif

#include "compiler.h"
#include "code.h"	// for FRAME_PREV(), FRAM_OPTOP()

#if JDK_VER >= 12
extern sys_mon_t *monitorEnter2(ExecEnv *, uintptr_t);
extern int monitorExit2(ExecEnv *, uintptr_t);
#endif	// JDK_VER

extern long JavaStackSize;	// declared in threads.h


//
// Local Functions
//
static bool_t isSystemMethod(struct methodblock *mb);


/*
 * Compile and invoke the method.
 */
bool_t compileAndInvokeMethod(
	JHandle *o, struct methodblock *mb, int args_size, ExecEnv *ee,
	stack_item *var_base) {
#if JDK_VER >= 12
  sys_thread_t *self = EE2SysThread(ee);
#endif
  sys_mon_t *mon;

  CodeInfo *info;
  int access;
  int compilation_result;
  int invocation_count;
#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG)
  int runtime_debug = 0;

#if 1
  sysAssert(mb != NULL);
  runtime_debug = debugp(mb);
#else
  if (ee->current_frame) {
    runtime_debug = debugp(ee->current_frame->current_method);
  }
#endif
#endif	// RUNTIME_DEBUG

  info = (CodeInfo *)mb->CompiledCodeInfo;
  mon = info->monitor;

#ifdef COMPILE_DEBUG
  printf("\n");
  printf("cmpl&InvMtd: 0x%x, %s#%s %s\n", mb,
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
  printf("  sys_th: %x sys_mon_t *: %x\n", (int)sysThreadSelf(), (int)mon);
  fflush(stdout);
  if (runtime_debug) {
    showStackFrames(ee);
  }
#endif

  // check stack overflow
  if(!sysThreadCheckStack()) {
    SignalError(ee, JAVAPKG "StackOverflowError", 0);
    return FALSE;
  }

  access = mb->fb.access;

  // compile the called method
  SYS_MONITOR_ENTER(self, mon);
  {
    void *invoker;

    // add an invocation counter
    invocation_count = info->invocation_count;
    invocation_count++;
    info->invocation_count = invocation_count;
#ifdef COMPILE_DEBUG
    printf("  invocation count: %d\n", invocation_count);  fflush(stdout);
#endif

    invoker = (void *)mb->invoker;
    if (invoker == sym_invokeJITCompiledMethod) {
#ifdef COMPILE_DEBUG
      printf("  already compiled.\n");  fflush(stdout);
#endif
      SYS_MONITOR_EXIT(self, mon);
      goto compilation_done;
    }
    else if ((invoker != sym_compileAndInvokeMethod)	// being compiled
#ifndef IGNORE_DISABLE
	     || (!compiler_enabled)		// compiler is disabled
#endif	// IGNORE_DISABLE
	     ) {
#ifdef COMPILE_DEBUG
      if (invoker != sym_compileAndInvokeMethod)
	printf("  the method is being compiled now.\n");
#  ifndef IGNORE_DISABLE
      else if (!compiler_enabled)
	printf("  compiler disabled.\n");
#  endif
#endif
      SYS_MONITOR_EXIT(self, mon);
      goto candi_call_normal_method;
    }
    // lazy compilation
    else if (!(mb->fb.access & ACC_STRICT)) {
      if (isSystemMethod(mb)) {		// system methods
	if ((invocation_count < opt_systhreshold)) {
#ifdef COMPILE_DEBUG
	  printf("  %d < %d (sys threshold).\n",
		invocation_count, opt_systhreshold);
#endif
	  SYS_MONITOR_EXIT(self, mon);
	  goto candi_call_normal_method;
	}
      }
      else {				// not system methods
	if ((invocation_count < opt_userthreshold)) {
#ifdef COMPILE_DEBUG
	  printf("  %d < %d (user threshold).\n",
		invocation_count, opt_userthreshold);
#endif
	  SYS_MONITOR_EXIT(self, mon);
	  goto candi_call_normal_method;
	}
      }
    }	// ! ACC_STRICT
  }

  // compile
  mb->invoker = access2invoker(access);
	// the method being compiled is to be interpreted
#ifdef COMPILE_DEBUG
  printf("  now compile.\n");  fflush(stdout);
#endif
  SYS_MONITOR_EXIT(self, mon);

  {
    JavaFrame *frame = ee->current_frame;
    frame->optop += args_size;	// to save real args
	// not to be broken args by method invocation during compilation
    compilation_result = compileMethod(mb, STAGE_DONE);
	// invoker is set to invokeJITCompiledMethod()
    frame->optop -= args_size;	// restore real optop
  }

  if (compilation_result) {
      // fail to compile...
#ifdef COMPILE_DEBUG
    printf("  compilation failed...\n");  fflush(stdout);
#endif
    mb->invoker = access2invoker(access);
    goto candi_call_normal_method;
  }

compilation_done:

#ifdef COMPILE_DEBUG
  printf("compileAndInvokeMethod() now call invoker (%s):\n"
	 "  %s#%s %s\n",
	nameOfInvoker(mb->invoker),
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
  fflush(stdout);
#endif
  {
    bool_t ret;
    ret = ((bool_t (*)(JHandle*,struct methodblock *,int,ExecEnv*,stack_item*))
		mb->invoker)
	(o, mb, args_size, ee, var_base);

    DECL_GLOBAL_FUNC(SYMBOL(candi_compiled_done));

    return ret;
  }

candi_call_normal_method:
  {
    bool_t (*norm_invoker)(JHandle*,struct methodblock *,int,ExecEnv*);
    bool_t invoker_ret;

    norm_invoker = access2invoker(access);
#ifdef COMPILE_DEBUG
    printf("call original invoker: %s(0x%x)\n",
		nameOfInvoker(norm_invoker), (int)norm_invoker);
    fflush(stdout);
#endif
    invoker_ret = norm_invoker(o, mb, args_size, ee);
    if (!invoker_ret)  return FALSE;

    if (!(access & (ACC_ABSTRACT | ACC_NATIVE))) {
	// normal Java method

      // restack is done in invocationHelper() (in runtime.c)
      // if this method is called by compiled method

      int exec_ret;
      stack_item *optop, *old_optop;
      int retsize;
      JavaFrame *cur_frame;;

#ifdef HPROF
      // profiling
#  if JDK_VER >= 12
      if (jvmpi_event_flags & JVMPI_EVENT_METHOD_ENTRY_ON)
	jvmpi_method_entry(ee, o);
#  else
      if (java_monitor)  ee->current_frame->mon_starttime = now();
#  endif	// JDK_VER
#endif	// HPROF

#ifdef RUNTIME_DEBUG
      if (runtime_debug) {
	printf("call ExecJava(invoker.c): %s#%s.\n",
		cbName(fieldclass(&mb->fb)), mb->fb.name);
	fflush(stdout);
      }
#endif
      exec_ret = pExecuteJava(mb->code, ee);
#ifdef RUNTIME_DEBUG
      if (runtime_debug) {
	printf("ExecJava done(invoker.c): %s#%s.\n",
		cbName(fieldclass(&mb->fb)), mb->fb.name);
	fflush(stdout);
      }
#endif

#if defined(WORKAROUND_FOR_FREEBSD_131P6) && (JDK_VER >= 12) && (defined(__FreeBSD__) || defined(__NetBSD__))
    // unblock SIGTRAP to get around a problem of FreeBSD JDK 1.3.1-p6
    {
      sigset_t set;
      sigemptyset(&set);
      sigaddset(&set, SIGTRAP);
      sigprocmask(SIG_UNBLOCK, &set, NULL);
    }
#endif

      cur_frame = ee->current_frame;
      old_optop = cur_frame->optop;
#if defined(EXECUTEJAVA_IN_ASM) && (JDK_VER < 12)
      if (cur_frame->monitor) {
	monitorExit2(ee, (MONITOR_T)cur_frame->monitor);
      }
#endif	// EXECUTEJAVA_IN_ASM
      cur_frame = cur_frame->prev;
      ee->current_frame = cur_frame;
      if (!exec_ret) {
#ifdef RUNTIME_DEBUG
	printf("  interpreter returned false.\n");
	if (exceptionOccurred(ee))
	  printf("  clazz of exc: %s\n",
		cbName(ee->exception.exc->methods->classdescriptor));
	fflush(stdout);
#endif
	return FALSE;
      }

#ifdef EXECUTEJAVA_IN_ASM
#if JDK_VER >= 12
      if (executejava_in_asm) {
#endif
	// This operation is required
	// only with x86 assembly ver. of executeJava.c
	retsize = ((CodeInfo *)mb->CompiledCodeInfo)->ret_size;
	if (retsize != 0) {
	  optop = cur_frame->optop;
	  if (retsize == 1) {
	    optop[0] = old_optop[-1];
#  ifdef RUNTIME_DEBUG
	    if (runtime_debug) {
	      printf("  optop[0]: %08x\n", optop[0].i);
	      fflush(stdout);
	    }
#  endif
	    optop++;
	  }
	  else {	// retsize == 2
	    optop[0] = old_optop[-2];  optop[1] = old_optop[-1];
#  ifdef RUNTIME_DEBUG
	    if (runtime_debug) {
	      printf("  optop[0,1]: %08x,%08x\n", optop[0].i, optop[1].i);
	      fflush(stdout);
	    }
#  endif
	    optop += 2;
	  }
	  cur_frame->optop = optop;
	}	// if (retsize != 0)
#if JDK_VER >= 12
      }	// if (executejava_in_asm)
#endif
#endif	// EXECUTEJAVA_IN_ASM
    }	// normal Java method

    return invoker_ret;
  }
}


/*
 * Invoke the compiled method.
 */
bool_t invokeJITCompiledMethod(
	JHandle *o, struct methodblock *mb, int args_size, ExecEnv *ee,
	stack_item *var_base_read_only) {
  stack_item *var_base;
  JavaFrame *old_frame, *frame;
  CodeInfo *info;
#ifdef RUNTIME_DEBUG
  int runtime_debug;
#endif

#ifdef RUNTIME_DEBUG
  runtime_debug = debugp(mb);
#endif

#ifdef RUNTIME_DEBUG
  if (runtime_debug) {
    printf("\n");
    printf("invokeJITCompiledMethod() called by %x:\n  %s#%s %s 0x%x\n",
	(int)ee,
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature, (int)mb);
    if (exceptionOccurred(ee)) {
      printf("  An exception not handled remains!\n");
    }
    fflush(stdout);
#if 1
    showStackFrames(ee);
#endif
  }	// if (runtime_debug)
#endif	// RUNTIME_DEBUG


  // check stack overflow
  if(!sysThreadCheckStack()) {
    SignalError(ee, JAVAPKG "StackOverflowError", 0);
    return FALSE;
  }


  info = (CodeInfo *)mb->CompiledCodeInfo;

  // add an invocation counter
  info->invocation_count++;


  old_frame = ee->current_frame;

  // creates a new frame
  CREATE_JAVAFRAME(ee, mb, old_frame, frame, args_size,
			mb->nlocals, mb->maxstack);
#ifndef DIRECT_INVOCATION
  frame->returnpc = (unsigned char *)-1;
	// mark as a frame for compiled code
#endif

#ifdef DIRECT_INVOCATION
  frame->constant_pool = NULL;
#endif


  // call

#ifndef DIRECT_INVOCATION
  if (((int)old_frame->returnpc) == -1) {
    // called by compiled method
    var_base = var_base_read_only;
    goto restack2native_done;
  }
#endif

  // called by normal Java or native method
  // restack from JVM stack to native stack
  {
    char *argsizes;
    stack_item *vars, *args;

    // allocate local var. space
    var_base = (stack_item *)alloca(mb->nlocals * 4);
    var_base += mb->nlocals - 1;

    vars = var_base;
    argsizes = ((CodeInfo *)mb->CompiledCodeInfo)->argsizes;
    args = frame->vars;	// points to JVM stack

    while (*argsizes) {
      if (*argsizes == 1) {
	vars--[0] = args++[0];
      }
      else {
	vars[-1] = args[0];  vars[0] = args[1];
	vars -= 2;  args += 2;
      }
      argsizes++;
    }	// while (*argsizes)
  }	// restack

restack2native_done:

#ifdef RUNTIME_DEBUG
  if (runtime_debug) {
    printf("  nlocals: %d\n", mb->nlocals);
    printf("  o:0x%08x, mb:0x%08x, args_size:%d, ee:0x%08x\n",
	(int)o, (int)mb, args_size, (int)ee);
    printf("  mb->CompiledCode: 0x%08x\n", (int)mb->CompiledCode);
    printf("  code size: 0x%08x\n", (info ? info->code_size : 0));
    fflush(stdout);

    showArguments(mb, var_base);
  }
#endif	// RUNTIME_DEBUG


#ifdef HPROF
  // profiling
#  if JDK_VER >= 12
  if (jvmpi_event_flags & JVMPI_EVENT_METHOD_ENTRY_ON)
    jvmpi_method_entry(ee, o);
#  else
  if (java_monitor)  frame->mon_starttime = now();
#  endif	// JDK_VER
#endif	// HPROF

#ifdef RUNTIME_DEBUG
  ((void (*) (JHandle*,struct methodblock*,int,ExecEnv*,stack_item*,int))
	mb->CompiledCode)(o, mb, args_size, ee, var_base, runtime_debug);
#else
  ((void (*) (JHandle*,struct methodblock*,int,ExecEnv*,stack_item*))
	mb->CompiledCode)(o, mb, args_size, ee, var_base);
#endif
DECL_GLOBAL_FUNC(SYMBOL(invokeJIT_compiled_done));

#if 0	// print edx
asm("pushal\n\t"
    "pushl  %edx");
asm("pushl %0" : : "m" ("edx: %08x\n") : "esi");
asm("call  printf@PLT\n\t"
    "addl  $8,%esp");
asm("popal");
#endif

  // optop[1] = ecx
  // optop[0] = edx
  asm("movl  %0,%%edi" : : "m" (old_frame) : "edi");	// edi = old_frame
  asm("movl  " FRAME_OPTOP(%edi) ",%eax");	// eax = old_frame->optop

  asm("movl  %edx,(%eax)\n\t"	// optop[0] = edx
      "movl  %ecx,4(%eax)");	// optop[1] = ecx

  ee->current_frame = old_frame;
  old_frame->optop += info->ret_size;	// retsize can be 0, 1 or 2

#ifdef HPROF
#  if JDK_VER >= 12
  if (JVMPI_EVENT_IS_ENABLED(JVMPI_EVENT_METHOD_EXIT))
    jvmpi_method_exit(ee);
#  else
  if (java_monitor)
    java_mon(old_frame->current_method, mb, now() - frame->mon_starttime);
#  endif	// JDK_VER
#endif	// HPROF

#ifdef RUNTIME_DEBUG
  if (runtime_debug) {
    printf("compiled method finished by");
    fflush(stdout);
    printf(" %x:", (int)sysThreadSelf());
    fflush(stdout);
    printf(" %s#%s %s\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
    fflush(stdout);

    printf("  free memory: handle %lld obj %lld\n",
		FreeHandleMemory(), FreeObjectMemory());
    fflush(stdout);

    if (exceptionOccurred(ee)) {
      JHandle *exc = ee->exception.exc;
      printf("  exception occurred:\n");
      printf("    0x%08x(%s)\n",
		(int)exc, cbName(exc->methods->classdescriptor));
      showExcStackTrace(exc);
      fflush(stdout);
    }
    else {
      if (info->ret_size != 2) {
	stack_item *optop = old_frame->optop;

	printf("  ret val: 0x%x (%s)", optop[-1].i, info->ret_sig);
	showObjectBody(info->ret_sig, optop[-1].h);
	printf("\n");
	fflush(stdout);
      }
    }

    printf("\n");
    fflush(stdout);
  }
#endif


#ifdef RUNTIME_DEBUG
  runtime_debug = 0;
#endif

  return (ee->exceptionKind == EXCKIND_NONE);	// !(exceptionOccurred(ee))
}


//
// Local Functions
//
static bool_t isSystemMethod(struct methodblock *mb) {
  char *cname = cbName(fieldclass(&mb->fb));

  if (strncmp("java/", cname, 5) && strncmp("sun/", cname, 4) &&
      strncmp("javax/", cname, 6) && strncmp("com/sun/", cname, 8) &&
      strncmp("org/omg/", cname, 8)) {
    return FALSE;
  }
  else {
    return TRUE;
  }
}



#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG)
bool_t debugp(struct methodblock *mb) {
  int debugp = FALSE;

  if (!mb)  return FALSE;

#if 1
  if ((!strcmp(cbName(fieldclass(&mb->fb)), "java/lang/Compiler")))
    debugp = TRUE;
  else if ((!strcmp(cbName(fieldclass(&mb->fb)), "java/lang/Object"))
	&& (!strncmp(mb->fb.name, "notify", 6)))
    debugp = TRUE;
  else if ((!strcmp(cbName(fieldclass(&mb->fb)), "java/lang/Thread")))
    debugp = TRUE;
  else if ((!strcmp(cbName(fieldclass(&mb->fb)), "java/lang/ThreadGroup")) &&
	   (
	     !strcmp(mb->fb.name, "uncaughtException") ||
	     !strcmp(mb->fb.name, "<init>") ||
	     !strcmp(mb->fb.name, "add") ||
	     !strcmp(mb->fb.name, "remove")
	     ))
    debugp = TRUE;
#if 0
  else if ((!strcmp(cbName(fieldclass(&mb->fb)), "NET/shudo/metavm/Proxy")))
    debugp = TRUE;
  else if ((!strcmp(cbName(fieldclass(&mb->fb)), "NET/shudo/metavm/Skeleton")))
    debugp = TRUE;
#endif
#if 0
  else if ((!strcmp(cbName(fieldclass(&mb->fb)), "org/openorb/CORBA/Any"))
	&& (!strcmp(mb->fb.name, "write_value") ||
	    !strncmp(mb->fb.name, "insert_", 7) ||
	    !strcmp(mb->fb.name, "type") ||
	    !strcmp(mb->fb.name, "<init>")))
    debugp = TRUE;
  else if ((!strcmp(cbName(fieldclass(&mb->fb)), "org/openorb/io/StreamHelper"))
	&& (!strcmp(mb->fb.name, "copy_stream")))
    debugp = TRUE;
#endif
#  if 0
  else if (!strcmp(mb->fb.name, "main"))
    debugp = TRUE;
  else if (!strcmp(mb->fb.name, "start"))
    debugp = TRUE;
  else if (!strcmp(mb->fb.name, "run"))
    debugp = TRUE;
  else if (!strcmp(mb->fb.name, "findNative"))
    debugp = TRUE;
#  endif
#endif

  return debugp;
}
#endif	// RUNTIME_DEBUG


#ifdef _WIN32
// Glue function between GCC's _alloca and VC++'s _chkstk.
volatile void *_alloca_wrapper(int size) {
  asm(".globl __alloca\n"
      ".def	__chkstk;	.scl	2;	.type	32;	.endef\n"
      ".def	__alloca;	.scl	2;	.type	32;	.endef\n"
      "__alloca:\n"
      "jmp __chkstk");
}
#endif
