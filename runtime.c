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

#include "compiler.h"

#ifdef METAVM
#include "metavm/metavm.h"	// for GET_REMOTE_FLAG()
#include "metavm/NET_shudo_metavm_Proxy_old.h"
#endif	// METAVM

extern long JavaStackSize;	// declared in threads.h


/*
 * returns: none
 */
void invocationHelper(
	JHandle *obj, struct methodblock *method, int args_size, ExecEnv *ee,
	stack_item *var_base
#ifdef RUNTIME_DEBUG
	, int runtime_debug
#endif
) {
  void *invoker;
  CodeInfo *info;
  JavaFrame *cur_frame = ee->current_frame;
  int retsize;
  int access;

  // get CompiledCodeInfo
  info = (CodeInfo *)method->CompiledCodeInfo;
#ifdef RUNTIME_DEBUG
  if (!info) {
    printf("WARNING: CompiledCodeInfo is NULL. (invocationHelper())\n");
    fflush(stdout);
    printf("  %s#%s %s\n", cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature);
    fflush(stdout);
  }
#endif
  sysAssert(method->CompiledCodeInfo != NULL);

  retsize = info->ret_size;

#ifdef RUNTIME_DEBUG
  if (runtime_debug) {
    struct methodblock *cur_mb;  char *sig;
    int i;

    printf("  invoker: %s (0x%x)\n",
	nameOfInvoker(method->invoker), (int)(method->invoker));

    access = method->fb.access;

    cur_mb = cur_frame->current_method;
    sig = method->fb.signature;

    printf("  local var base: 0x%x\n", (int)var_base);
    printf("    0x%x\n", *(int32_t *)var_base);
    printf("  ee: 0x%08x\n", (int)ee);
    printf("  current_frame: 0x%08x\n", (int)cur_frame);
    printf("  caller method:");
    fflush(stdout);
    if (cur_mb)
      printf(" %s#%s %s",
		cbName(fieldclass(&cur_mb->fb)),
		cur_mb->fb.name, cur_mb->fb.signature);
    else
      printf(" (null)");
    fflush(stdout);
    printf(" (0x%08x)\n", (int)cur_mb);
    fflush(stdout);

    printf("  obj: 0x%x\n", obj);  fflush(stdout);
    if (obj) {
      printf("    ");
      if (access & ACC_STATIC) {
	printf("%s", unhand((Hjava_lang_Class *)obj)->name);
      }
      else if (obj_flags(obj) == T_NORMAL_OBJECT) {
	printf("%s", cbName(obj->methods->classdescriptor));
      }
      else {	// array object
	printf("array");
      }
      printf(" (0x%x)", (int)obj);
    }
    else
      printf("(null)");
    printf("\n");
    printf("  method: ");
    printf("%s#%s %s (%x)\n",
	cbName(fieldclass(&method->fb)), method->fb.name, sig, (int)method);
    fflush(stdout);
    printf("  acc: 0x%x", access);
    if (access & ACC_NATIVE)  printf(" native");
    if (access & ACC_MACHINE_COMPILED)  printf(" machine_compiled");
    if (access & ACC_PUBLIC)  printf(" public");
    if (access & ACC_PRIVATE)  printf(" private");
    if (access & ACC_PROTECTED)  printf(" protected");
    if (access & ACC_STATIC)  printf(" static");
    if (access & ACC_FINAL)  printf(" final");
    if (access & ACC_SYNCHRONIZED)  printf(" synchronized");
    if (access & ACC_ABSTRACT)  printf(" abstract");
    printf("\n");
    printf("  retsize, args_size, nlocals: %d, %d, %d\n",
		retsize, args_size, method->nlocals);
    fflush(stdout);
    i = 0;
    if (!(access & ACC_STATIC)) {
      printf("    0x%08x @ 0x%08x  ", (int)var_base[0].h, (int)var_base);
      fflush(stdout);
      showObjectBody("L;", var_base[0].h);
      printf("\n");
      fflush(stdout);
      i++;
    }
    if (sig[0] == '(')  sig++;
    for (; i < args_size; i++) {
      JHandle *arg = var_base[-i].h;
      if ((sig[0] == 'J') || (sig[0] == 'D')) {
	stack_item *argp;

	i++;
	argp = var_base - i;

	if (sig[0] == 'J')
	  printf("    %lld,0x%llx", *((long long *)argp));
	else
	  printf("    %10g", *((double *)argp));
	printf(" @ 0x%08x\n", argp);

	sig++;
      }
      else {
	printf("    0x%08x @ 0x%08x  ", (int)arg, (int)(var_base - i));
	fflush(stdout);
	sig = showObjectBody(sig, arg);
	printf("\n");
      }
      fflush(stdout);
    }
  }
#endif	// RUNTIME_DEBUG


#define CALL_INVOKER \
  {\
    register int ret asm("eax");\
    ret = ((bool_t (*)(JHandle*,struct methodblock*,int,ExecEnv*,stack_item*))\
	invoker)(obj, method, args_size, ee, var_base);\
    if (!ret)  goto invhelper_return;\
  }

  invoker = method->invoker;
#ifndef DIRECT_INVOCATION
  if (invoker == sym_invokeJITCompiledMethod) {	// compiled method is called
    CALL_INVOKER;
    goto invhelper_finish;
  }
#endif	// DIRECT_INVOCATION

  info = (CodeInfo *)method->CompiledCodeInfo;
  retsize = info->ret_size;

#ifdef DIRECT_INV_NATIVE
  if ((invoker == sym_invokeJNINativeMethod) ||
      (invoker == sym_invokeJNISynchronizedNativeMethod)
#  if JDK_VER >= 12
      || (((uint32_t)invoker >= sym_invokeJNI_min) &&
	  ((uint32_t)invoker <= sym_invokeJNI_max))
#  endif
      ) {
	// JNI native method
    JavaFrame *old_frame, *frame;
    stack_item *optop;
    JNIEnv *env;
    ClassClass **is_static;
    int is_sync;
    char *terse_sig;
    unsigned char *code;
    int argsize;

    old_frame = ee->current_frame;
    optop = old_frame->optop;
    // create a new frame
    CREATE_JAVAFRAME_FOR_NATIVE(ee, method, old_frame, frame, args_size);

    env = EE2JNIEnv(ee);

    access = method->fb.access;
    is_sync = access & ACC_SYNCHRONIZED;
    is_static = ((access & ACC_STATIC) ? (ClassClass **)&obj : NULL);

    terse_sig = info->terse_sig;
    code = method->code;

    argsize = method->args_size + 1 /* for (JNIEnv *) */;
    if (is_static)  argsize++;
    argsize <<= 2;

    if (is_sync) {
      monitorEnter2(ee, obj_monitor(obj));
    }

#ifdef RUNTIME_DEBUG
#  define PUSH_CONSTSTR(STR) \
    {\
      static char *msg = STR;\
      asm("pushl %0" : : "m" (msg) : "esi");\
    }

    if (runtime_debug) {
      printf("call JNI: %s#%s %s\n", cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature);
      printf("  argsize: %d\n", argsize);
      printf("  old_frame: %x\n", old_frame);
      printf("  optop: %x\n", optop);
      printf("  ee,env: %x,%x\n", ee,env);
      printf("  obj: %x ((var_base): %x)\n", obj, var_base[0].i);
      printf("  code: %x\n", method->code);
      printf("  is_static: %x\n", is_static);
      fflush(stdout);

      asm("pushl %eax\n\t"
	  "leal  4(%esp),%eax");
      asm("pushal");
      asm("pushl %eax");
      PUSH_CONSTSTR("  ESP: %x\n");
      asm("call " FUNCTION(printf) "\n\t"
	  "addl $8,%esp");
      asm("popal");
      asm("popl  %eax");
    }
#endif
    // esi: src addr., edi: dst addr.
    asm("movl  %0,%%edx\n\t"		// edx = argsize
	"subl  %%edx,%%esp\n\t"		// esp -= argsize
	"movl  %1,%%esi\n\t"		// esi = var_base
	"movl  %%esp,%%edi\n\t"		// edi = esp
	"movl  %%esi,%%ecx\n\t"		// ecx = var_base
	: : "m" (argsize), "m" (var_base)
	: "edx", "esi", "edi", "esp", "ecx");

    asm("movl  %0,%%edx\n\t"		// edx = env
	"movl  %%edx,(%%edi)\n\t"	// *dst = edx
	"addl  $4,%%edi"
	: : "m" (env)
	: "edx", "edi");
    asm("movl  %0,%%edx\n\t"		// edx = is_static
	"testl %%edx,%%edx\n\t"
	"jnz   jni_static\n\t"
	"subl  $4,%%esi\n\t"
	"jmp   jni_static_done\n\t"
      "jni_static:\n\t"
	"movl  %%edx,%%ecx\n\t"		// ecx = is_static
      "jni_static_done:\n\t"
	"movl  %%ecx,(%%edi)\n\t"	// *dst = var_base or is_static
	"addl  $4,%%edi"
	: : "m" (is_static)
	: "edx", "esi", "ecx", "edi");
#ifdef RUNTIME_DEBUG
    // if (runtime_debug)
    asm("cmpl  $0,%0\n\t"
	"je    debug_jni_obj_done" : : "m" (runtime_debug));
    asm("pushal\n\t"
	"pushl %ecx");
    PUSH_CONSTSTR("  obj: %x\n");
    asm("call  printf@PLT\n\t"
	"addl  $8,%esp\n\t"
	"popal\n\t"
      "debug_jni_obj_done:");
#endif

    asm("movl  %0,%%edx" : : "m" (terse_sig) : "edx");	// edx = terse_sig

    asm("xorl  %%ecx,%%ecx\n\t"	// ecx = 0
      "jniarg_loop:\n\t"
	"movb  (%%edx),%%cl\n\t"	// ecx = *terse_sig
	"incl  %%edx\n\t"
	"jmp   *jniarg_table(,%%ecx,4)\n\t"
      "jniarg_obj:\n\t"
	"movl  (%%esi),%%eax\n\t"
	"testl %%eax,%%eax\n\t"
	"jz    jniarg_obj_null\n\t"
	"movl  %%esi,%%eax\n\t"
      "jniarg_obj_null:\n\t"
	"movl  %%eax,(%%edi)\n\t"
#ifdef RUNTIME_DEBUG
	: : : "ecx", "eax");
    // if (runtime_debug)
    asm("cmpl  $0,%0\n\t"
	"je    debug_jniarg_obj_done" : : "m" (runtime_debug));
    asm("pushal\n\t"
	"pushl %eax");
    PUSH_CONSTSTR("    argobj: %x\n");
    asm("call  printf@PLT\n\t"
	"addl  $8,%esp\n\t"
	"popal\n\t"
      "debug_jniarg_obj_done:");
    asm(
#endif
	"subl  $4,%%esi\n\t"
	"addl  $4,%%edi\n\t"
	"jmp   jniarg_loop\n\t"
      "jniarg_32:\n\t"
	"movl  (%%esi),%%eax\n\t"
#ifdef RUNTIME_DEBUG
	: : : "esi", "edi", "eax");
    // if (runtime_debug)
    asm("cmpl  $0,%0\n\t"
	"je    debug_jniarg_32_done" : : "m" (runtime_debug));
    asm("pushal\n\t"
	"pushl %eax");
    PUSH_CONSTSTR("    arg32: %x\n");
    asm("call  printf@PLT\n\t"
	"addl  $8,%esp");
    asm("popal\n\t"
      "debug_jniarg_32_done:");
    asm(
#endif
	"subl  $4,%%esi\n\t"
	"movl  %%eax,(%%edi)\n\t"
	"addl  $4,%%edi\n\t"
	"jmp   jniarg_loop\n\t"
      "jniarg_64:\n\t"
	"movl  -4(%%esi),%%eax\n\t"
	"movl  %%eax,(%%edi)\n\t"
	"movl  (%%esi),%%eax\n\t"
	"movl  %%eax,4(%%edi)\n\t"
#ifdef RUNTIME_DEBUG
	: : : "esi", "edi", "eax");
    // if (runtime_debug)
    asm("cmpl  $0,%0\n\t"
	"je    debug_jniarg_64_done" : : "m" (runtime_debug));
    asm("pushal\n\t"
	"pushl 4(%edi)\n\tpushl (%edi)");
    PUSH_CONSTSTR("    arg64: %x %x\n");
    asm("call  printf@PLT\n\t"
	"addl  $12,%esp\n\t"
	"popal\n\t"
      "debug_jniarg_64_done:");
    asm(
#endif
	"subl  $8,%%esi\n\t"
	"addl  $8,%%edi\n\t"
	"jmp   jniarg_loop\n\t"
      "jniarg_done:"
	: : : "ecx", "eax", "esi", "edi");

#ifdef RUNTIME_DEBUG
    // if (runtime_debug)
    asm("cmpl  $0,%0\n\t"
	"je    debug_jniarg_done" : : "m" (runtime_debug));
    asm("pushl %eax\n\t"
	"leal  4(%esp),%eax\n\t"
	"pushal");
    asm("pushl 16(%eax)\n\tpushl 12(%eax)\n\t"
	"pushl 8(%eax)\n\tpushl 4(%eax)\n\tpushl (%eax)");
    PUSH_CONSTSTR("  (esp): %x %x %x %x %x\n");
    asm("call  printf@PLT\n\t"
	"addl  $24,%esp\n\t"
	"popal\n\tpopl %eax\n\t"
      "debug_jniarg_done:");
#endif
    // call Java_...()
    asm("movl  %edx,%edi");	// save terse_sig
#ifndef _WIN32
    asm("call  *%0\n\t"		// call Java_...()
	"movl  %1,%%ecx\n\t"		// ecx = argsize
	"addl  %%ecx,%%esp"		// esp += argsize
	: : "m" (code), "m" (argsize)
	: "edx", "edi", "ecx", "esp");
#else
    asm("call  *%0" : : "m" (code));		// call Java_...()
#endif	// !_WIN32
#ifdef RUNTIME_DEBUG
    // if (runtime_debug)
    asm("cmpl  $0,%0\n\t"
	"je    debug_jni_call_done" : : "m" (runtime_debug));
    asm("subl $200,%esp\n\tpushal");
    asm("pushl %edx\n\tpushl %eax");
    PUSH_CONSTSTR("  eax:edx %x:%x\n");
    asm("call  printf@PLT\n\t"
	"addl  $12,%esp\n\t"
	"popal\n\taddl $200,%esp\n\t"
      "debug_jni_call_done:");
#endif

#ifdef RUNTIME_DEBUG
    // if (runtime_debug)
    asm("cmpl  $0,%0\n\t"
	"je    debug_jni_argsize_done" : : "m" (runtime_debug));
    asm("subl $200,%esp\n\tpushal\n\t"
	"pushl %ecx");
    PUSH_CONSTSTR("  argsize: %x\n");
    asm("call  printf@PLT\n\t"
	"addl  $8,%esp\n\t"
	"popal\n\taddl $200,%esp\n\t"
      "debug_jni_argsize_done:");
#endif
    asm("xorl  %ecx,%ecx\n\t"		// ecx = 0
	"movb  (%edi),%cl");
#ifdef RUNTIME_DEBUG
    // if (runtime_debug)
    asm("cmpl  $0,%0\n\t"
	"je    debug_terse_done" : : "m" (runtime_debug));
    asm("subl $200,%esp\n\tpushal\n\t"
	"pushl %ecx");
    PUSH_CONSTSTR("  terse ret: %d\n");
    asm("call  printf@PLT\n\t"
	"addl  $8,%esp\n\t"
	"popal\n\taddl $200,%esp\n\t"
      "debug_terse_done:");
#endif
    asm("movl  %0,%%esi\n\t"		// esi = optop
	"jmp   *jniret_table(,%%ecx,4)"
	: : "m" (optop)
	: "ecx", "esi");
    asm("jniret_void:\n\t"
	"movl  %esi,%eax\n\t"
	"jmp   jniret_done\n\t"
      "jniret_obj:");
#ifdef RUNTIME_DEBUG
    // if (runtime_debug)
    asm("cmpl  $0,%0\n\t"
	"je    debug_retobj_done" : : "m" (runtime_debug));
    asm("subl $200,%esp\n\tpushal\n\t"
	"pushl %eax");
    PUSH_CONSTSTR("  retobj: %x\n");
    asm("call  printf@PLT\n\t"
	"addl  $8,%esp\n\t"
	"popal\n\taddl $200,%esp\n\t"
      "debug_retobj_done:");
#endif
    asm("testl %eax,%eax\n\t"
	"jz    jniret_int32\n\t"
	"movl  (%eax),%eax");
#ifdef _WIN32
    asm("jniret_s8:");
    CAST_INT8_TO_INT32(%eax);
    asm("jmp   jniret_int32");
    asm("jniret_u8:");
    CAST_UINT8_TO_INT32(%eax);
    asm("jmp   jniret_int32");
    asm("jniret_s16:");
    CAST_INT16_TO_INT32(%eax);
    asm("jmp   jniret_int32");
    asm("jniret_u16:");
    CAST_UINT16_TO_INT32(%eax);
    asm("jmp   jniret_int32");
#endif
    asm("jniret_int32:");
#ifdef RUNTIME_DEBUG
    // if (runtime_debug)
    asm("cmpl  $0,%0\n\t"
	"je    debug_ret32_done" : : "m" (runtime_debug));
    asm("subl $200,%esp\n\tpushal\n\t"
	"pushl %eax");
    PUSH_CONSTSTR("  ret32: %x\n");
    asm("call  printf@PLT\n\t"
	"addl  $8,%esp\n\t"
	"popal\n\taddl $200,%esp\n\t"
      "debug_ret32_done:");
#endif
    asm("movl  %eax,(%esi)\n\t"	// *optop = eax
	"leal  4(%esi),%eax\n\t"	// eax = optop + 4
	"jmp   jniret_done\n\t"
      "jniret_fp32:\n\t"
	"fstps (%esi)\n\t"
	"leal  4(%esi),%eax\n\t"	// eax = optop + 4
	"jmp   jniret_done\n\t"
      "jniret_int64:\n\t"
	"movl  %eax,(%esi)\n\t"
	"leal  8(%esi),%eax\n\t"	// eax = optop + 8
	"movl  %edx,4(%esi)");
#ifdef RUNTIME_DEBUG
    // if (runtime_debug)
    asm("cmpl  $0,%0\n\t"
	"je    debug_ret64_done" : : "m" (runtime_debug));
    asm("subl $200,%esp\n\tpushal\n\t"
	"pushl 4(%esi)\n\tpushl (%esi)");
    PUSH_CONSTSTR("  ret64: %08x %08x\n");
    asm("call  printf@PLT\n\t"
	"addl  $12,%esp\n\t"
	"popal\n\taddl $200,%esp\n\t"
      "debug_ret64_done:");
#endif
    asm("jmp   jniret_done\n\t"
      "jniret_fp64:\n\t"
	"fstpl (%esi)\n\t"
	"leal  8(%esi),%eax\n\t"	// eax = optop + 8
	"jmp   jniret_done");

    asm("jniarg_table:\n\t"
	".long jniarg_done\n\t"
	".long jniarg_obj\n\t"
	".long jniarg_64\n\t"
	".long jniarg_64\n\t"
	".long jniarg_32\n\t"
	".long jniarg_32\n\t"
	".long jniarg_32\n\t"
	".long jniarg_32\n\t"
	".long jniarg_32\n\t"
	".long jniarg_32\n\t"
	".long jniarg_done\n\t"
	".long jniarg_done\n\t"
      "jniret_table:\n\t"
	".long jniret_done\n\t"
	".long jniret_obj\n\t"
	".long jniret_int64\n\t"
	".long jniret_fp64\n\t"
#ifdef _WIN32
	".long jniret_u8\n\t"	// boolean
	".long jniret_s8\n\t"	// byte
	".long jniret_s16\n\t"	// short
	".long jniret_u16\n\t"	// char
#else
	".long jniret_int32\n\t"
	".long jniret_int32\n\t"
	".long jniret_int32\n\t"
	".long jniret_int32\n\t"
#endif
	".long jniret_int32\n\t"
	".long jniret_fp32\n\t"
	".long jniret_void\n\t"
	".long jniret_done");

    asm("jniret_done:");

    {
      register stack_item *eax asm("%eax");
      optop = eax;
    }
    ee->current_frame = old_frame;

    if (is_sync) {
      monitorExit2(ee, obj_monitor(obj));
    }

#ifdef RUNTIME_DEBUG
    if (runtime_debug) {
      printf("JNI done: %s %s#%s\n", cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature);
      fflush(stdout);
    }
#endif
    if (!exceptionOccurred(ee)) {
      old_frame->optop = optop;
	// update frame->optop only in case that an exception occurred
      goto invhelper_finish;
    }
    goto invhelper_return;
  }
#endif	// DIRECT_INV_NATIVE
#ifdef DIRECT_INV_NATIVE
  else if ((invoker == sym_invokeNativeMethod) ||
	   (invoker == sym_invokeSynchronizedNativeMethod)) {
	// old fashion (NMI) native method
    JavaFrame *old_frame;
    stack_item *optop;
    int is_static, is_sync;
    char *terse_sig;
    char *code;
    int argsize;

#ifdef RUNTIME_DEBUG
    if (runtime_debug) {
      printf("call NMI: %s#%s %s\n", cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature);
      fflush(stdout);
    }
#endif
    code = info->code;
    if (!code) {
      // resolve an address of old the native method not wrapped by stub
      char buf[300];  char *limit = buf + 300,  *bufp = buf;
      bufp += mangleUTFString(cbName(fieldclass(&method->fb)),
			bufp, limit - bufp, MangleUTF_Class);
      if (limit - bufp > 1)  *bufp++ = '_';
      bufp += mangleUTFString(method->fb.name, bufp, limit - bufp,
				MangleUTF_Field);

      code = (char *)symbolInSystemClassLoader(buf);
#ifdef RUNTIME_DEBUG
      if (runtime_debug) {
	printf("  func: %s (%x)\n", buf, code);
	fflush(stdout);
      }
#endif
      if (!code)  goto invoke_via_invoker;
      info->code = code;
    }

    old_frame = ee->current_frame;
    optop = old_frame->optop;
#if 0
    // create a new frame
    CREATE_JAVAFRAME_FOR_NATIVE(ee, method, old_frame, frame, args_size);
#endif

    access = method->fb.access;
    is_sync = access & ACC_SYNCHRONIZED;
    is_static = access & ACC_STATIC;

    terse_sig = info->terse_sig;

    argsize = method->args_size;
    if (is_static)  argsize++;
    argsize <<= 2;

    if (is_sync) {
      monitorEnter2(ee, obj_monitor(obj));
    }

    // esi: src addr., edi: dst addr.
    asm("movl  %0,%%edx\n\t"
	"subl  %%edx,%%esp\n\t"
	"movl  %1,%%esi\n\t"
	"movl  %%esp,%%edi"
	: : "m" (argsize), "m" (var_base)
	: "edx", "esi", "edi", "esp");

    asm("movl  %0,%%edx\n\t"		// edx = is_static
	"testl %%edx,%%edx\n\t"
	"jnz   nmi_static\n\t"
	"movl  %1,%%eax\n\t"		// eax = obj
	"subl  $4,%%esi\n\t"
	"jmp   nmi_static_done\n\t"
      "nmi_static:\n\t"
	"xorl  %%eax,%%eax\n\t"		// eax = NULL
      "nmi_static_done:\n\t"
	"movl  %%eax,(%%edi)\n\t"	// *dst = obj or NULL
	"addl  $4,%%edi"
	: : "m" (is_static), "m" (obj)
	: "edx", "eax", "esi", "edi");

    asm("movl  %0,%%edx" : : "m" (terse_sig) : "edx");	// edx = terse_sig

    asm("xorl  %%ecx,%%ecx\n\t"	// ecx = 0
      "nmiarg_loop:\n\t"
	"movb  (%%edx),%%cl\n\t"	// ecx = *terse_sig
	"incl  %%edx\n\t"
	"jmp   *nmiarg_table(,%%ecx,4)\n\t"
      "nmiarg_32:\n\t"
	"movl  (%%esi),%%eax\n\t"
	"subl  $4,%%esi\n\t"
	"movl  %%eax,(%%edi)\n\t"
	"addl  $4,%%edi\n\t"
	"jmp   nmiarg_loop\n\t"
      "nmiarg_64:\n\t"
	"movl  -4(%%esi),%%eax\n\t"
	"movl  %%eax,(%%edi)\n\t"
	"movl  (%%esi),%%eax\n\t"
	"movl  %%eax,4(%%edi)\n\t"
	"subl  $8,%%esi\n\t"
	"addl  $8,%%edi\n\t"
	"jmp   nmiarg_loop\n\t"
      "nmiarg_done:"
	: : : "ecx", "eax", "esi", "edi");

    // call native function
    asm("movl  %%edx,%%edi\n\t"	// save terse_sig
	"call  *%0\n\t"		// call
	"movl  %1,%%ecx\n\t"	// ecx = argsize
	"addl  %%ecx,%%esp"	// esp += argsize
	: : "m" (code), "m" (argsize)
	: "edx", "edi", "ecx", "esp");

    asm("xorl  %%ecx,%%ecx\n\t"		// ecx = 0
	"movb  (%%edi),%%cl\n\t"
	"movl  %0,%%esi\n\t"		// esi = optop
	"jmp   *nmiret_table(,%%ecx,4)"
	: : "m" (optop));
    asm("nmiret_void:\n\t"
	"movl  %esi,%eax\n\t"
	"jmp   nmiret_done");
#ifdef _WIN32
    asm("nmiret_s8:");
    CAST_INT8_TO_INT32(%eax);
    asm("jmp   nmiret_int32");
    asm("nmiret_u8:");
    CAST_UINT8_TO_INT32(%eax);
    asm("jmp   nmiret_int32");
    asm("nmiret_s16:");
    CAST_INT16_TO_INT32(%eax);
    asm("jmp   nmiret_int32");
    asm("nmiret_u16:");
    CAST_UINT16_TO_INT32(%eax);
    asm("jmp   nmiret_int32");
#endif
    asm("nmiret_int32:\n\t"
	"movl  %eax,(%esi)\n\t"
	"leal  4(%esi),%eax\n\t"
	"jmp   nmiret_done\n\t"
      "nmiret_fp32:\n\t"
	"fstps (%esi)\n\t"
	"leal  4(%esi),%eax\n\t"
	"jmp   nmiret_done\n\t"
      "nmiret_int64:\n\t"
	"movl  %eax,(%esi)\n\t"
	"leal  8(%esi),%eax\n\t"
	"movl  %edx,4(%esi)");
#ifdef RUNTIME_DEBUG
    if (runtime_debug) {
      asm("subl $200,%esp\n\tpushal\n\t"
	  "pushl 4(%esi)\n\tpushl (%esi)");
      PUSH_CONSTSTR("  ret64: %08x %08x\n");
      asm("call  printf@PLT\n\t"
	  "addl  $12,%esp");
      asm("popal\n\taddl $200,%esp");
    }
#endif	// RUNTIME_DEBUG
    asm("jmp   nmiret_done\n\t"
      "nmiret_fp64:\n\t"
	"fstpl (%esi)\n\t"
	"leal  8(%esi),%eax\n\t"
	"jmp   nmiret_done");

    asm("nmiarg_table:\n\t"
	".long nmiarg_done\n\t"
	".long nmiarg_32\n\t"
	".long nmiarg_64\n\t"
	".long nmiarg_64\n\t"
	".long nmiarg_32\n\t"
	".long nmiarg_32\n\t"
	".long nmiarg_32\n\t"
	".long nmiarg_32\n\t"
	".long nmiarg_32\n\t"
	".long nmiarg_32\n\t"
	".long nmiarg_done\n\t"
	".long nmiarg_done\n\t"
      "nmiret_table:\n\t"
	".long nmiret_done\n\t"
	".long nmiret_int32\n\t"
	".long nmiret_int64\n\t"
	".long nmiret_fp64\n\t"
#ifdef _WIN32
	".long nmiret_u8\n\t"	// boolean
	".long nmiret_s8\n\t"	// byte
	".long nmiret_s16\n\t"	// short
	".long nmiret_u16\n\t"	// char
#else
	".long nmiret_int32\n\t"
	".long nmiret_int32\n\t"
	".long nmiret_int32\n\t"
	".long nmiret_int32\n\t"
#endif
	".long nmiret_int32\n\t"
	".long nmiret_fp32\n\t"
	".long nmiret_void\n\t"
	".long nmiret_done");

    asm("nmiret_done:");

    {
      register stack_item *eax asm("%eax");
      optop = eax;
    }
#if 0
    ee->current_frame = old_frame;
#endif

    if (is_sync) {
      monitorExit2(ee, obj_monitor(obj));
    }

#ifdef RUNTIME_DEBUG
    if (runtime_debug) {
      printf("NMI done: %s#%s %s\n", cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature);
      fflush(stdout);
    }
#endif
    if (!exceptionOccurred(ee)) {
      old_frame->optop = optop;
	// update frame->optop only in case that an exception occurred
      goto invhelper_finish;
    }
    goto invhelper_return;
  }
  else
#endif	// DIRECT_INV_NATIVE
  {
	// normal Java, native method or compileAndInvokeMethod()
    stack_item *sp, *optop;
    char *argsizes;

  invoke_via_invoker:
    // restack from native stack to JVM stack
    sp = var_base;
    optop = ee->current_frame->optop;

    argsizes = info->argsizes;
    while (*argsizes) {
      if (*argsizes == 1) { optop[0] = sp[0];  optop++;  sp--; }
      else { optop[0] = sp[-1];  optop[1] = sp[0];  optop += 2;  sp -= 2; }
      argsizes++;
    }

#if defined(WORKAROUND_FOR_FREEBSD_131P6) && (JDK_VER >= 12) && (defined(__FreeBSD__) || defined(__NetBSD__))
    // unblock SIGTRAP to get around a problem of FreeBSD JDK 1.3.1-p6
    {
      sigset_t set;
      sigemptyset(&set);
      sigaddset(&set, SIGTRAP);
      sigprocmask(SIG_UNBLOCK, &set, NULL);
    }
#endif

    CALL_INVOKER;

    if ((invoker == sym_invokeJavaMethod) ||
	(invoker == sym_invokeSynchronizedJavaMethod)) {
	// normal Java method
      int exec_ret;
      stack_item *old_optop;

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
      if (runtime_debug) {
	printf("call ExecuteJava(runtime.c): %s#%s.\n",
		cbName(fieldclass(&method->fb)), method->fb.name);
	fflush(stdout);
      }
#endif
      exec_ret = pExecuteJava(method->code, ee);
#ifdef RUNTIME_DEBUG
      if (runtime_debug) {
	printf("ExecuteJava(runtime.c) done: %s#%s.\n",
		cbName(fieldclass(&method->fb)), method->fb.name);
	fflush(stdout);
      }
#endif
      cur_frame = ee->current_frame;	// load
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
	if (runtime_debug) {
	  printf("  ExecuteJava() returned false.\n");
	  if (exceptionOccurred(ee))
	    printf("  clazz of exc: %s\n",
		cbName(ee->exception.exc->methods->classdescriptor));
	  fflush(stdout);
	}
#endif
	goto invhelper_return;
      }

#ifdef EXECUTEJAVA_IN_ASM
#if JDK_VER >= 12
      if (executejava_in_asm) {
#endif
	// This operation is required
	// only with x86 assembly ver. of executeJava.c
	{
	  stack_item *optop;
	  if (retsize != 0) {
	    optop = cur_frame->optop;
	    if (retsize == 1) {
	      optop[0] = old_optop[-1];  optop++;
	    }
	    else {	// retsize == 2
	      optop[0] = old_optop[-2];  optop[1] = old_optop[-1];  optop += 2;
	    }
	    cur_frame->optop = optop;
	  }
	}
#if JDK_VER >= 12
      }	// if (executejava_in_asm)
#endif
#endif	// EXECUTEJAVA_IN_ASM
    }	// normal Java method
  }	// normal Java or native method or compileAndInvokeMethod()



invhelper_finish:

#ifdef RUNTIME_DEBUG
  // if (runtime_debug)
  asm("cmpl  $0,%0\n\t"
      "je    debug_invhelper_done" : : "m" (runtime_debug));
  {
    stack_item *optop;
    optop = ee->current_frame->optop;

    if (retsize > 0) {
      printf("  optop[-1]: 0x%x %d %g\n",
		optop[-1].i, optop[-1].i, optop[-1].f);
      if (retsize > 1) {
	printf("  optop[-2]: 0x%x %d %g\n",
		optop[-2].i, optop[-2].i, optop[-2].f);
      }
      fflush(stdout);
    }

    if (retsize != 2) {
      printf("  ret val: ");
      showObjectBody(((CodeInfo *)method->CompiledCodeInfo)->ret_sig,
			optop[-1].h);
      printf("\n");
      fflush(stdout);
    }
  }
  asm("debug_invhelper_done:");
#endif

  // set return value to %edx and %ecx
  {
    JavaFrame *frame = ee->current_frame;
    stack_item *optop;
    sysAssert((retsize >= 0) && (retsize <= 2));
    optop = (frame->optop -= retsize);

    asm("movl  %0,%%edx" : : "m" (optop[0].i) : "edx");
    asm("movl  %0,%%ecx" : : "m" (optop[1].i) : "ecx");

    return;

    // same as:
    // if (retsize == 1) {
    //   %edx = frame->optop[-1].i;			// state 1
    //   frame->optop--;
    // }
    // else if (retsize == 2) {
    //   %ecx = optop[-1].i;  %edx = optop[-2].i;	// state 4
    //   frame->optop -= 2;
    // }
  }

invhelper_return:
  return;
}


/*
 * returns: method to be invoked, NULL if an exception occurred.
 */
struct methodblock *getInterfaceMethod(
	JHandle *obj, ExecEnv *ee,
	struct methodblock *imethod, unsigned char *guessptr
#ifdef INVINTF_INLINE_CACHE
	, unsigned char *cache_ptr
#endif
#ifdef RUNTIME_DEBUG
	, int runtime_debug
#endif
) {
  struct methodblock *method;
  ClassClass *intfClazz;
  unsigned int offset;

  ClassClass *cb;
  struct methodtable *mtable;
  struct imethodtable *imtable;
  int guess;

  intfClazz = fieldclass(&imethod->fb);
  offset = imethod->fb.u.offset;
  guess = *guessptr;
#ifdef RUNTIME_DEBUG
  if (runtime_debug) {
    printf("  intf method: %s#%s %s, guess: %d\n",
	cbName(intfClazz), imethod->fb.name, imethod->fb.signature,
	guess);
    fflush(stdout);
  }
#endif

#if 0	// null check is performed by compiled code
  if (!obj) {
    SignalError(ee, JAVAPKG "NullPointerException", 0);
    return NULL;
  }
#endif

#if 0	// Currently, we can assume the receiver is not an array.
		// An array class implements Cloneable and Serializable,
		// but those interfaces have no method.
  if (obj_flags(obj) == T_NORMAL_OBJECT) {
#endif	// 0
    mtable = obj_methodtable(obj);  cb = mtable->classdescriptor;
#ifdef METAVM
    if ((mtable == proxy_methodtable) && (GET_REMOTE_FLAG(ee))) {
      // obj instanceof Proxy
      cb = unhand((HNET_shudo_metavm_Proxy *)obj)->clz;
      mtable = cbMethodTable(cb);
    }
#endif	// METAVM
#if 0	// Currently, we can assume the receiver is not an array.
  }
  else {
    cb = classJavaLangObject;  mtable = cbMethodTable(cb);
  }
#endif	// 0
  imtable = cbIntfMethodTable(cb);

  if (// guess >= 0 && guess < imtable->icount &&	// be always the case
	imtable->itable[guess].classdescriptor == intfClazz) {
    // `guess' has been already calculated.
    goto getintf_done;
  }
  else {
    for (guess = imtable->icount - 1; ;guess--) {
      if (guess < 0) {
	invokeInterfaceError(ee, guessptr - 4, cb, intfClazz);
	return NULL;
      }
      if (imtable->itable[guess].classdescriptor == intfClazz) {
	*guessptr = (unsigned char)guess;
	break;
      }
    }
  }

getintf_done:
  method = mt_slot(mtable, imtable->itable[guess].offsets[offset]);

#ifdef INVINTF_INLINE_CACHE
  // lock
#if JDK_VER >= 12
  CODE_LOCK(EE2SysThread(ee));
#else
  BINCLASS_LOCK();
#endif	// JDK_VER

  // code patching
  *(uint32_t *)(cache_ptr - 22) = (uint32_t)cb;
  *(uint32_t *)(cache_ptr - 11) = (uint32_t)method;

  // unlock
#if JDK_VER >= 12
  CODE_UNLOCK(EE2SysThread(ee));
#else
  BINCLASS_UNLOCK();
#endif	// JDK_VER
#endif	// INVINTF_INLINE_CACHE

#ifdef RUNTIME_DEBUG
  if (runtime_debug) {
    printf("  getInterfaceMethod() returns %s#%s\n",
		cbName(fieldclass(&method->fb)), method->fb.name);
    fflush(stdout);
  }
#endif

  return method;
}


JHandle *multianewarray(
#ifdef RUNTIME_DEBUG
	int runtime_debug,
#endif
	ExecEnv *ee, int dim, ClassClass *arrayclazz,
	stack_item *stackpointer) {
  stack_item *optop = ee->current_frame->optop;
  int i;

  stackpointer += (dim - 1);
  for (i = 0; i < dim; i++) {
    int size = stackpointer->i;
#ifdef RUNTIME_DEBUG
    if (runtime_debug) {
      printf("  %d: %d\n", i, size);
      fflush(stdout);
    }
#endif
#ifndef NO_NULL_AND_ARRAY_CHECK
    if (size < 0)	// NegativeArraySizeException
      return (JHandle *)-1;
#endif
    optop[i].i = size;
    stackpointer--;
  }

  return MultiArrayAlloc(dim, arrayclazz, optop);
}


#if defined(PATCH_ON_JUMP) || defined(PATCH_WITH_SIGTRAP)
int once_InitClass(ExecEnv *ee, ClassClass *cb) {
  sysAssert(cb != NULL);

  // class initialization
#  if !defined(INITCLASS_IN_COMPILATION) && (defined(PATCH_ON_JUMP) || defined(PATCH_WITH_SIGTRAP))
  if (!CB_INITIALIZED(cb))  InitClass(cb);
#  endif

  return exceptionOccurred(ee);
}
#endif


#if (JDK_VER < 12)
void InitClass(ClassClass *cb) {
  char *detail = 0;
#ifdef RUNTIME_DEBUG
  printf("InitClass(%s).\n", (cb?cbName(cb):"(null)"));
  fflush(stdout);
#endif

  initializeClassForJIT(cb, TRUE, FALSE);
	// This is necessary because methodblock->info can be required
	// before ResolveClass() calls InitializeForCompiler()

  ResolveClass(cb, &detail);
}
#endif	// JDK_VER


struct CatchFrame *searchCatchFrame(
	ExecEnv *ee, struct methodblock *mb, int bytepcoff
#ifdef RUNTIME_DEBUG
	, int runtime_debug
#endif
) {
  ClassClass *methodClazz;
  cp_item_type *constant_pool;
  unsigned char *type_table;
  struct CatchFrame *cf;
  int found;
  int i;

#ifdef RUNTIME_DEBUG
  if (runtime_debug) {
    printf("  searchCatchFrame() called:\n"
	   "    ee: 0x%x, mb: 0x%x, pc: %d(0x%x)\n",
	(int)ee, (int)mb, bytepcoff, bytepcoff);
    fflush(stdout);
  }
#endif

  methodClazz = fieldclass(&mb->fb);
  constant_pool = cbConstantPool(methodClazz);
  type_table = constant_pool[CONSTANT_POOL_TYPE_TABLE_INDEX].type;

#ifdef RUNTIME_DEBUG
  if (runtime_debug) {
    JHandle *exc = ee->exception.exc;
    printf("  exc: 0x%08x ", exc);
    fflush(stdout);
    if (exc)
      printf("(%s)\n", cbName(exc->methods->classdescriptor));
    else
      printf(" is NULL !!!\n");
    printf("  bytepcoff %d\n", bytepcoff);
    fflush(stdout);
  }
#endif

  found = 0;	// false
  cf = mb->exception_table;
  for (i = 0; i < mb->exception_table_length; i++) {
#ifdef RUNTIME_DEBUG
    if (runtime_debug) {
      printf("  pc: start %ld end %ld type %d\n",
		cf->start_pc, cf->end_pc, cf->catchType);
      fflush(stdout);
    }
#endif
    if ((cf->start_pc <= bytepcoff) && (cf->end_pc > bytepcoff)) {
      int typeindex = cf->catchType;
      char *catchName;
      ClassClass *catchClazz = NULL;
      ClassClass *excClazz;

      if (ee->exception.exc == NULL) {
	found = 1;  goto search_finish;
      }

      if (typeindex == 0) {
		// This catch frame accpets any type of exceptions.
	found = 1;  goto search_finish;
      }

      catchName = GetClassConstantClassName(constant_pool, typeindex);
#ifdef RUNTIME_DEBUG
      if (runtime_debug) {
	printf("    catch name: %s\n", catchName);
	fflush(stdout);
      }
#endif

      for (excClazz = obj_array_classblock(ee->exception.exc);
		excClazz; excClazz = cbSuperclass(excClazz)) {
#ifdef RUNTIME_DEBUG
	if (runtime_debug) {
	  printf("      exc class: %s\n", cbName(excClazz));
	  printf("      loader of exc: 0x%x, current method: 0x%x\n",
		cbLoader(excClazz), cbLoader(methodClazz));
	  fflush(stdout);
	}
#endif
	if ( !(strcmp(cbName(excClazz), catchName)) &&
		(cbLoader(excClazz) == cbLoader(methodClazz)) ) {
	  found = 1;  goto search_finish;
	}

	if (!catchClazz) {
	  if (!CONSTANT_POOL_TYPE_TABLE_IS_RESOLVED(type_table, typeindex)) {
	    bool_t ret_resolve;
	    char exceptionKindCache;

#ifdef RUNTIME_DEBUG
	    printf("    not resolved yet: %d\n", typeindex);
	    fflush(stdout);
#endif
	    exceptionKindCache = ee->exceptionKind;
	    ee->exceptionKind = EXCKIND_NONE;
	    ret_resolve = ResolveClassConstantFromClass2(
		methodClazz, typeindex, ee, 1 << CONSTANT_Class, FALSE);
	    ee->exceptionKind = exceptionKindCache;

	    if (!ret_resolve) {
#ifdef RUNTIME_DEBUG
	      if (runtime_debug) {
		printf("    resolution of catch class failed: %d\n",
			typeindex);
		fflush(stdout);
	      }
#endif
	      continue;
	    }
	  }
#ifdef RUNTIME_DEBUG
	  else {
	    printf("    already resolved: %d\n", typeindex);
	    fflush(stdout);
	  }
#endif
	  catchClazz = constant_pool[typeindex].clazz;
	}
	if (excClazz == catchClazz) {
	  found = 1;  goto search_finish;
	}
      }	// for (excClazz = ...)
    }	// if (start_pc <= pc < end_pc)

    cf++;
  }	// for (i = 0; ...

search_finish:
  if (found) {
#ifdef RUNTIME_DEBUG
    if (runtime_debug) {
      printf("  exc. handler is found: %s#%s %ld-%ld type:%d\n"
		"  native offset 0x%x(%d).\n",
		cbName(fieldclass(&mb->fb)), mb->fb.name,
		cf->start_pc, cf->end_pc, (int)cf->catchType,
		(int)cf->compiled_CatchFrame, (int)cf->compiled_CatchFrame);
      fflush(stdout);
    }
#endif
    exceptionClear(ee);

    return cf;
  }
  else {
#ifdef RUNTIME_DEBUG
    if (runtime_debug) {
      printf("  exc. handler is not found at method %s#%s.\n",
		cbName(fieldclass(&mb->fb)), mb->fb.name);
      fflush(stdout);
    }
#endif
    return NULL;
  }
}


void showStackFrames(ExecEnv *ee) {
  JavaFrame *frame;

  if (!ee) {
    printf("showStackFrames(): ee is NULL\n");
    fflush(stdout);
    return;
  }
  printf("stack frames (ee:%x)\n", (int)ee);
  fflush(stdout);
  frame = ee->current_frame;
  while (frame) {
    struct methodblock *frameMb = frame->current_method;
    if (frameMb) {
      printf("  %s#%s %s 0x%x  ", cbName(fieldclass(&frameMb->fb)),
	frameMb->fb.name, frameMb->fb.signature, (int)frameMb);
      fflush(stdout);
      if (frameMb->invoker == sym_invokeJITCompiledMethod) {
	if (frame->lastpc)
	  printf("pc: %ld (compiled)",
		frame->lastpc - (int)frameMb->CompiledCode);
      }
      else if ((frameMb->invoker == sym_invokeNativeMethod) ||
		(frameMb->invoker == sym_invokeSynchronizedNativeMethod))
	printf(" (old native)");
      else if ((frameMb->invoker == sym_invokeJNINativeMethod) ||
		(frameMb->invoker == sym_invokeJNISynchronizedNativeMethod)
#if JDK_VER >= 12
	       || (((uint32_t)frameMb->invoker >= sym_invokeJNI_min) &&
		   ((uint32_t)frameMb->invoker <= sym_invokeJNI_max))
#endif
	       ) {
	printf(" (JNI native)");
      }
      else {	// invoke{,Synchronized}JavaMethod
	if (frame->lastpc)
	  printf("pc: %ld", frame->lastpc - frameMb->code);
      }
    }
    else {
      printf("  (null)  lastpc: 0x%08x", (int)frame->lastpc);
    }

    printf("  optop: 0x%x\n", (int)frame->optop);

    fflush(stdout);

    frame = frame->prev;
  }
}


#ifdef RUNTIME_DEBUG
void showArguments(struct methodblock *mb, stack_item *vars) {
  char *sig = mb->fb.signature;
  int args_size = mb->args_size;
  int i = 0;

  if (args_size > 0) {
    printf("  args:\n");

    if (!(mb->fb.access & ACC_STATIC)) {
      printf("    0x%08x @ 0x%08x  ", (int)vars[0].h, (int)vars);
      showObjectBody("L;", vars[0].h);
      printf("\n");
      i++;
    }

    sig = mb->fb.signature;
    if (sig[0] == '(')  sig++;

    for (; i < args_size; i++) {
      stack_item *argp = vars - i;

      if ((sig[0] == 'J') || (sig[0] == 'D')) {
	i++;
	argp -= 1;

	if (sig[0] == 'J')
	  printf("    %lld,0x%llx", *((long long *)argp));
	else
	  printf("    %10g", *((double *)argp));
	printf(" @ 0x%08x\n", argp);

	sig++;
      }
      else {
	printf("    0x%08x @ 0x%08x  ", (int)argp->h, (int)argp);
	sig = showObjectBody(sig, argp->h);
	printf("\n");
      }
    }
  }
  fflush(stdout);
}
#endif	// RUNTIME_DEBUG


#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG)
typedef struct Classjava_lang_Throwable {
  struct Hjava_lang_Object *backtrace;
  struct Hjava_lang_String *detailMessage;
} Classjava_lang_Throwable;
HandleTo(java_lang_Throwable);

void showExcStackTrace(JHandle *o) {
#if JDK_VER < 12
  HArrayOfByte *backtrace =
	(HArrayOfByte *)(unhand((Hjava_lang_Throwable *)o)->backtrace);
  if (backtrace) {
    unsigned char **data = (unsigned char **)(unhand(backtrace)->body);
    unsigned char **end = (unsigned char **)(data + obj_length(backtrace));
#else
  HArrayOfObject *backtrace =
	(HArrayOfObject *)(unhand((Hjava_lang_Throwable *)o)->backtrace);
  HArrayOfInt *hdata;
  if (backtrace)  hdata = (HArrayOfInt *)(unhand(backtrace)->body[0]);
  if (hdata) {
    unsigned char **data = (unsigned char **)(unhand(hdata)->body);
    unsigned char **end = (unsigned char **)(data + obj_length(hdata));
#endif	// JDK_VER
    for (; data < end; data++) {
      if (*data) {
#define BUF_SIZE	256
	char buf[BUF_SIZE];
	char *p = buf;
	struct methodblock *mb = methodByPC(*data);

	strncpy(buf, "\tat ", 4);  p += 4;
	if (mb) {
	  p += snprintf(p, buf + BUF_SIZE - p, "%s#%s %s  ",
		cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
	  p += snprintf(p, buf + BUF_SIZE - p, "pc: %d",
		*data - mb->code);
	  if (mb->invoker == sym_invokeJITCompiledMethod)
	    p += snprintf(p, buf + BUF_SIZE - p, "(compiled)");
	}
	else {
	  p += snprintf(p, buf + BUF_SIZE - p, "(null)");
	}
	printf(buf);
	printf("\n");
      }
    }
  }
}
#endif	// RUNTIME_DEBUG || COMPILE_DEBUG


#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG) || defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)
struct methodblock *methodByPC(unsigned char *pc) {
  struct methodblock *mb;
  ClassClass *cb;
  int i, j;

#if JDK_VER >= 12
  BINCLASS_LOCK(sysThreadSelf());
#else
  BINCLASS_LOCK();
#endif
  for (i = nbinclasses; --i >= 0; ) {
    cb = binclasses[i];
    for (mb = cbMethods(cb), j = cbMethodsCount(cb); --j >= 0; mb++) {
      if (mb->fb.access & ACC_NATIVE) {	// native method
	if (mb->code == pc)  goto done;
      }
      else {				// not native method
	if (mb->fb.access & ACC_MACHINE_COMPILED)	// compiled method
	  if (PCinCompiledCode(pc, mb))
	    goto done;
	else {
	  unsigned char *code = (unsigned char *)mb->code;
	  if (pc >= code && pc < code + mb->code_length)
	    goto done;
	}
      }
    }
  }
  mb = 0;

done:
#if JDK_VER >= 12
  BINCLASS_UNLOCK(sysThreadSelf());
#else
  BINCLASS_UNLOCK();
#endif
  return mb;
}
#endif	// RUNTIME_DEBUG || COMPILE_DEBUG || EXC_SIGNAL || GET_SIGCONTEXT


#ifdef RUNTIME_DEBUG
char *showObjectBody(char *sig, JHandle *obj) {
  switch (*sig) {
  case '[':
    sig++;
    if (*sig == 'L')  while (*(sig++) != ';');
    else sig++;
#ifdef METAVM
    if (obj)
      if (obj_flags(obj) == T_NORMAL_OBJECT)
	goto show_obj;
#endif	// METAVM
    if (!obj)
      printf("(null)");
    else {
      char buf[256];
show_array:
      printf("len %ld", obj_length(obj));
      switch (obj_flags(obj)) {
      case T_BYTE:
	printf(" [B `%s'", unhand((HArrayOfByte *)obj)->body);
	break;
      case T_CHAR:
	printf(" [C `%s'",
		unicode2utf(unhand((HArrayOfChar *)obj)->body, obj_length(obj),
			 buf, 256));
	break;
      }
      printf(" flags %x", obj_flags(obj));
    }
    break;
  case 'L':
    while (*(sig++) != ';');
show_obj:
    if (!obj)
      printf("(null)");
    else {
      ClassClass *cb;
      char *classname;
      if (obj_flags(obj) != T_NORMAL_OBJECT)  goto show_array;
      if (obj->methods) {
	classname = cbName(obj_array_classblock(obj));
	printf("class %s", classname);
	if (!strcmp(classname, "java/lang/String")) {
	  int strlen = javaStringLength((Hjava_lang_String *)obj);
	  printf(" len %d", strlen);
	  if (strlen) {
	    char buf[256];
	    javaString2CString((Hjava_lang_String *)obj, buf, 256);
	    printf(" `%s'", buf);
	  }
	}
	else if (!strcmp(classname, "java/lang/Class")) {
	  printf(" (%s)", cbName((ClassClass *)obj));
	}
      }	// obj->methods
    }
    break;

  case 'V':
    printf("(void)"); sig++;
    break;
  case 'I':
    printf("%d", (int)obj);  sig++;
    break;
  case 'F':
    printf("%d", *((float *)&obj));  sig++;
    break;

  default:
    sig++;
    break;
  }

  return sig;
}
#endif	// RUNTIME_DEBUG
