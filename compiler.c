/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1998,1999,2000,2001,2002,2003,2004 Kazuyuki Shudo

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

#include <stdlib.h>	// for getenv(),atoi(),atexit()
#include <sys/stat.h>	// for gdbm_open(), stat()
#include <unistd.h>	// for stat()
#include <string.h>	// for memset()

#include "compiler.h"

#ifdef HAVE_POSIX_SCHED
#  include <sched.h>
#endif

#if defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)
#  include <signal.h>
#endif	// EXC_BY_SIGNAL || GET_SIGCONTEXT

#if JDK_VER >= 12
#  include "jit.h"	// for JITInterface, JITInterface6
#endif

#ifdef CODE_DB
#  include <fcntl.h>	// for O_...
#  include <dlfcn.h>	// for dl*()
#endif	// CODE_DB

#ifdef METAVM
#  include "jni.h"
#  include "metavm/metavm.h"	// for macro METAVM_PKG
#endif


//
// Global Variables
//
#if defined(linux) && (JDK_VER >= 12)
extern int _JVM_native_threads;
#endif

sys_mon_t *global_monitor;

#if JDK_VER >= 12
bool_t executejava_in_asm;
#endif

#ifndef IGNORE_DISABLE
bool_t compiler_enabled = TRUE;
#endif

// for strictfp
bool_t is_fpupc_double = FALSE;

#ifdef CODE_DB
#  ifdef GDBM
#    include <gdbm.h>
GDBM_FILE db = NULL;
#  else
#    include <ndbm.h>
DBM *db = NULL;
#  endif
int db_page = -1;
#endif	// CODE_DB

ClassClass *classJavaLangNoClassDefFoundError;
ClassClass *classJavaLangNoSuchFieldError;
ClassClass *classJavaLangNoSuchMethodError;

struct bool_opt_entry { char *name; int num; };
struct bool_opt_entry bool_opt_entry[] = {
  {"quiet", OPT_QUIET},
  {"igndisable", OPT_IGNDISABLE},
  {"outcode", OPT_OUTCODE},
  {"codesize", OPT_CODESIZE},
  {"cmplatload", OPT_CMPLATLOAD},
  {"cmplclinit", OPT_CMPLCLINIT},
  {"ignstrictfp", OPT_IGNSTRICTFP},
  {"frcstrictfp", OPT_FRCSTRICTFP},
  {"fpsingle", OPT_FPSINGLE},
  {"fpextended", OPT_FPEXTENDED},
#ifdef HAVE_POSIX_SCHED
  {"sched_fifo", OPT_SCHED_FIFO},
  {"sched_rr", OPT_SCHED_RR},
#endif
#ifdef CODE_DB
  {"codedb", OPT_CODEDB},
#endif
  {"ignlock", OPT_IGNLOCK},
  {NULL, -1}
};
int options = 0;
int opt_systhreshold = LAZY_COMPILATION_THRESHOLD_FOR_SYS;
int opt_userthreshold = LAZY_COMPILATION_THRESHOLD_FOR_USER;
#ifdef METHOD_INLINING
int opt_inlining_maxlen = METHOD_INLINING_MAXLEN;
int opt_inlining_depth = METHOD_INLINING_DEPTH;
#endif

void *sym_compileAndInvokeMethod, *sym_invokeJITCompiledMethod;
void *sym_invokeJavaMethod, *sym_invokeSynchronizedJavaMethod;
void *sym_invokeAbstractMethod;
void *sym_invokeNativeMethod, *sym_invokeSynchronizedNativeMethod;
void *sym_invokeJNINativeMethod, *sym_invokeJNISynchronizedNativeMethod;
void *sym_invokeLazyNativeMethod;
#if JDK_VER >= 12
uint32_t sym_invokeJNI_min, sym_invokeJNI_max;
#endif
#ifdef CODE_DB
#  ifdef GDBM
GDBM_FILE (*sym_dbm_open)(char *,int,int,int,void (*)());
void (*sym_dbm_close)(GDBM_FILE);
int (*sym_dbm_store)(GDBM_FILE,datum,datum,int);
datum (*sym_dbm_fetch)(GDBM_FILE,datum);
void (*sym_dbm_sync)(GDBM_FILE);
#  else
DBM *(*sym_dbm_open)(const char *,int,int);
void (*sym_dbm_close)(DBM *);
int (*sym_dbm_store)(DBM *,datum,datum,int);
datum (*sym_dbm_fetch)(DBM *,datum);
#  endif
#endif	// CODE_DB


//
// Local Functions
//
static void initializeClassHook(ClassClass *);
static void freeClass(ClassClass *);
static unsigned char *compiledCodePC(JavaFrame *, struct methodblock *);
static bool_t pcInCompiledCode(unsigned char *pc, struct methodblock *);
#ifdef DIRECT_INVOCATION
static JavaFrame *framePrev(JavaFrame *frame, JavaFrame *frame_buf);
#endif	// DIRECT_INVOCATION
#if JDK_VER >= 12
static void *frameID(JavaFrame *frame);
#endif
#ifndef IGNORE_DISABLE
static void compilerEnable();
static void compilerDisable();
#endif	// IGNORE_DISABLE
static bool_t compileClass(ClassClass *);
static bool_t compileClasses(Hjava_lang_String *);
static HObject *compilerCommand(HObject *);


/*
 * Initialization of the compiler.
 * This method is called by JVM.
 */
void java_lang_Compiler_start(
#if JDK_VER < 12
  void ** vector
#else
  JITInterface *jitinterface
#endif
) {
  ExecEnv *ee = EE();
  int version;
  int compilerVersion;

#ifdef COMPILE_DEBUG
  printf("java_lang_Compiler_start() called.\n");
  printf("  ee: 0x%x\n", ee);
#  if defined(linux) && (JDK_VER >= 12)
  printf("  _JVM_native_threads: %s\n", _JVM_native_threads ? "true":"false");
#  endif
  fflush(stdout);
#endif


  // prepare the global monitor
  global_monitor = (sys_mon_t *)sysCalloc(1, sysMonitorSizeof());
  sysMonitorInit(global_monitor);


#if JDK_VER >= 12
  // judge whether interpreter (asm or C) is used
#  ifdef DEBUG
  executejava_in_asm = FALSE;
#  else
  if (pExecuteJava == ExecuteJava_C)
    executejava_in_asm = FALSE;
  else if (pExecuteJava == ExecuteJava)
    executejava_in_asm = TRUE;
  else {
    printf("FATAL: could not determine whether interpreter is used, C version or asm version.\n");
    JVM_Exit(1);
  }
#  endif	// DEBUG
#ifdef COMPILE_DEBUG
  printf("interpreter: ");
  if (executejava_in_asm)
    printf("asm version.\n");
  else
    printf("C version.\n");
  fflush(stdout);
#endif
#endif	// JDK_VER


#if JDK_VER < 12
  // prevent unloading of java.lang.Compiler class
  {
    ClassClass *compiler_cb = FindClass(ee, "java/lang/Compiler", TRUE);
    if (compiler_cb == NULL) {
      printf("FATAL: cannot find java.lang.Compiler class.\n");
      if (exceptionOccurred(ee)) {
	JHandle *exc = ee->exception.exc;
	if (exc != NULL)
	  fprintf(stderr, "%s\n", cbName(exc->methods->classdescriptor));
#if JDK_VER >= 12
	printStackTrace(ee, 100, NULL);
#endif
	fflush(stderr);
      }
      JVM_Exit(1);
    }
    CCSet(compiler_cb, Sticky);
  }
#endif


#ifdef METAVM
  // force JVM to load Proxy class
  {
    ClassClass *proxy_cb;

#if 1
    proxy_cb = FindClass(ee, METAVM_PKG "Proxy", TRUE);
#else
    proxy_cb = LoadClassLocally(METAVM_PKG "Proxy");
#endif
    if (proxy_cb == NULL) {
      printf("FATAL: cannot find " METAVM_PKG "Proxy class.\n");
#if JDK_VER >= 12
      printf("You have to place MetaVM classes in JDK_DIR/jre/classes.\n");
#endif
      fflush(stdout);
      JVM_Exit(1);
    }

    // prevent unloading
    CCSet(proxy_cb, Sticky);
  }
#endif	// METAVM


  // version check

#if JDK_VER < 12
  version = *(int *)vector[0];
#else
  version = *(jitinterface->JavaVersion);
#endif
#ifdef COMPILE_DEBUG
  printf("  version num. of class file format: %d.%d\n",
	(version >> 16) & 0xff, version & 0xffff);
#endif	// COMPILE_DEBUG

  compilerVersion = version >> 24;

  if (compilerVersion != COMPILER_VERSION) {
    printf("warning: version num. of compiler interface is not %d: %d\n",
	COMPILER_VERSION, compilerVersion);
  }



  // get options
  {
    char *opts = getenv("JAVA_COMPILER_OPT");
    char *opt;

    if (opts) {
#ifdef RUNTIME_DEBUG
      printf("  JAVA_COMPILER_OPT: %s\n", opts);
      fflush(stdout);
#endif

      opt = strtok(opts, ", ");
      while (opt) {
	int i = 0;
	char *opt_name;
	while (opt_name = bool_opt_entry[i].name, opt_name) {
	  if (!strcmp(opt, opt_name)) {
	    OPT_SET(bool_opt_entry[i].num);
	    if (!OPT_SETQ(OPT_QUIET))  printf(" option: %s\n", opt_name);
	    break;
	  }
	  i++;
	}
	if (!strncmp(opt, "systhreshold=", 13)) {
	  opt_systhreshold = atoi(opt + 13);
	  if (!OPT_SETQ(OPT_QUIET))
	    printf(" option: systhreshold = %d\n", opt_systhreshold);
	}
	if (!strncmp(opt, "userthreshold=", 14)) {
	  opt_userthreshold = atoi(opt + 14);
	  if (!OPT_SETQ(OPT_QUIET))
	    printf(" option: userthreshold = %d\n", opt_userthreshold);
	}
#ifdef METHOD_INLINING
	else if (!strncmp(opt, "inlinemaxlen=", 13)) {
	  opt_inlining_maxlen = atoi(opt + 13);
	  if (!OPT_SETQ(OPT_QUIET))
	    printf(" option: inlinemaxlen = %d\n", opt_inlining_maxlen);
	}
	else if (!strncmp(opt, "inlinedepth=", 12)) {
	  opt_inlining_depth = atoi(opt + 12);
	  if (!OPT_SETQ(OPT_QUIET))
	    printf(" option: inlinedepth = %d\n", opt_inlining_depth);
	}
#endif	// METHOD_INLINING
	opt = strtok(NULL, ", ");
      }

      fflush(stdout);
    }
  }


#ifdef HAVE_POSIX_SCHED
  // set scheduler
  if (OPT_SETQ(OPT_SCHED_FIFO) || OPT_SETQ(OPT_SCHED_RR)) {
    int policy;
    struct sched_param schedp;

    if (OPT_SETQ(OPT_SCHED_FIFO))
      policy = SCHED_FIFO;
    else if (OPT_SETQ(OPT_SCHED_FIFO))
      policy = SCHED_RR;
    else {
      /* NOTREACHED */
      policy = SCHED_OTHER;
    }
    memset((void *)&schedp, 0, sizeof(schedp));
    schedp.sched_priority = sched_get_priority_max(policy);

    if (sched_setscheduler(0 /* self */, policy, &schedp) != 0) {
      perror("sched_setscheduler");
      printf("FATAL: `sched_fifo' and `sched_rr' options cannot work.\n");
      printf("       These options will need the root privilege.\n");
      JVM_Exit(1);
    }
  }
#endif


  // show credit
  if (!OPT_SETQ(OPT_QUIET)) {
    fprintf(stderr, CREDIT);
    fflush(stderr);
  }


  // set rounding precision
  {
    unsigned short cw;
    asm("fnstcw %0" : "=m"(cw));
    cw &= ~0x0300;

    if (OPT_SETQ(OPT_FPEXTENDED))
      cw |= 0x0300;
    else if (!OPT_SETQ(OPT_FPSINGLE))
      cw |= 0x0200;

    asm("fldcw %0" : : "m"(cw));
  }


  // prohibit generating lossy opcodes
  UseLosslessQuickOpcodes = TRUE;


  // resolve symbols
#define RESOLVE_A_INVOKER(NAME)	\
  if (!(sym_##NAME = (void *)symbolInSystemClassLoader(#NAME))) {\
    printf("FATAL: cannot resolve a symbol: " #NAME "\n");\
    JVM_Exit(1);\
  }

  RESOLVE_A_INVOKER(compileAndInvokeMethod);
  RESOLVE_A_INVOKER(invokeJITCompiledMethod);
  RESOLVE_A_INVOKER(invokeJavaMethod);
  RESOLVE_A_INVOKER(invokeSynchronizedJavaMethod);
  RESOLVE_A_INVOKER(invokeAbstractMethod);
  RESOLVE_A_INVOKER(invokeNativeMethod);
  RESOLVE_A_INVOKER(invokeSynchronizedNativeMethod);
  RESOLVE_A_INVOKER(invokeJNINativeMethod);
  RESOLVE_A_INVOKER(invokeJNISynchronizedNativeMethod);
  RESOLVE_A_INVOKER(invokeLazyNativeMethod);


#if JDK_VER >= 12
  {
    char *custom_invoker_names[] = {
// in TERSE_SIG_* (see include-old/signature.h)
	"\x1\x8\x8\xb\x8", "\x1\x1\xb\x1", "\xb\x1", "\x1\x8\x8\xb\xa",
	"\xb\x2", "\x1\x8\x1\x8\x8\xb\xa", "\x1\xb\x1", "\x3\xb\x3",
	"\xb\xa", "\x8\x8\x8\x8\xb\xa", "\x1\xb\xa", "\x8\xb\xa", "\xb\x8",
	"\xb\x4", NULL
// in string (see src/share/javavm/include/invokers.txt)
//	"OII_I", "OO_O", "V_O", "OII_V",
//	"V_J", "OIOII_V", "O_O", "D_D",
//	"V_V", "IIII_V", "O_V", "I_V", "V_I",
//	"V_Z", NULL
    };
    uint32_t sym;
    char *p;
    int i;

    sym_invokeJNI_min = (uint32_t)-1;
    sym_invokeJNI_max = 0;

    i = 0;
    while (p = custom_invoker_names[i]) {
      sym = (uint32_t)getCustomInvoker(p);
      if (sym) {
	if (sym > sym_invokeJNI_max)  sym_invokeJNI_max = sym;
	if (sym < sym_invokeJNI_min)  sym_invokeJNI_min = sym;
      }
      i++;
    }
  }
#endif	// JDK_VER


#ifdef COMPILE_DEBUG
#  define SHOW_A_INVOKER(NAME) \
  printf("  " #NAME ":\t0x%08x\n", (int)sym_##NAME)

  printf("symbols:\n");
  SHOW_A_INVOKER(compileAndInvokeMethod);
  SHOW_A_INVOKER(invokeJITCompiledMethod);
  SHOW_A_INVOKER(invokeJavaMethod);
  SHOW_A_INVOKER(invokeSynchronizedJavaMethod);
  SHOW_A_INVOKER(invokeAbstractMethod);
  SHOW_A_INVOKER(invokeNativeMethod);
  SHOW_A_INVOKER(invokeSynchronizedNativeMethod);
  SHOW_A_INVOKER(invokeJNINativeMethod);
  SHOW_A_INVOKER(invokeJNISynchronizedNativeMethod);
  SHOW_A_INVOKER(invokeLazyNativeMethod);
#endif


  // find classes
#define FIND_A_CLASS(NAME) \
  if ((classJavaLang##NAME =\
	FindClass(ee, JAVAPKG #NAME, TRUE)) == NULL) {\
    printf("FATAL: cannot find the class: " #NAME "\n");\
    JVM_Exit(1);\
  }

  FIND_A_CLASS(NoClassDefFoundError);
  FIND_A_CLASS(NoSuchFieldError);
  FIND_A_CLASS(NoSuchMethodError);


  // initialize all classes already loaded
  if (!OPT_SETQ(OPT_DONTCMPLVMCLZ)) {
    ClassClass **clazzptr;
    int i;

#if JDK_VER >= 12
    BINCLASS_LOCK(sysThreadSelf());
#else
    BINCLASS_LOCK();
#endif
#ifdef COMPILE_DEBUG
    printf("%d classes is already loaded.\n", nbinclasses);
    fflush(stdout);
#endif

    // initialize
    for (i = nbinclasses, clazzptr = binclasses; --i >= 0; clazzptr++) {
#ifdef COMPILE_DEBUG
      printf("init: %x %s\n", *clazzptr, cbName(*clazzptr));
      fflush(stdout);
#endif
      initializeClassForJIT(*clazzptr, FALSE, TRUE);
    }

    if (OPT_SETQ(OPT_CMPLATLOAD)) {
      // compile
      ClassClass **classes =
	(ClassClass **)sysMalloc(sizeof(ClassClass *) * nbinclasses);
      memcpy(classes, binclasses, sizeof(ClassClass *) * nbinclasses);

      for (i = nbinclasses, clazzptr = classes; --i >= 0; clazzptr++) {
#ifdef COMPILE_DEBUG
	printf("compile: %s\n", cbName(*clazzptr));  fflush(stdout);
#endif
	compileClass(*clazzptr);
      }

      sysFree(classes);
    }
#if JDK_VER >= 12
    else {
      // compile java.lang.ref.*
      ClassClass **classes =
	(ClassClass **)sysMalloc(sizeof(ClassClass *) * nbinclasses);
      memcpy(classes, binclasses, sizeof(ClassClass *) * nbinclasses);

      for (i = nbinclasses, clazzptr = classes; --i >= 0; clazzptr++) {
	if (!strncmp(cbName(*clazzptr), "java/lang/ref/", 14)) {
#ifdef COMPILE_DEBUG
	  printf("compile: %s\n", cbName(*clazzptr));  fflush(stdout);
#endif
	  compileClass(*clazzptr);
	}
      }

      sysFree(classes);
    }
#endif

#if JDK_VER >= 12
    BINCLASS_UNLOCK(sysThreadSelf());
#else
    BINCLASS_UNLOCK();
#endif
  }


  // initialize for signal handler
#if (defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)) && defined(SEARCH_SIGCONTEXT)
#  if JDK_VER < 12
  *(char **)vector[3] = (char *)examineSigcontextNestCount;
#  else
  *((JITInterface6 *)jitinterface)->p_CompiledCodeSignalHandler =
	examineSigcontextNestCount;
#  endif

  asm(".globl " SYMBOL(exam_nest) "\n\t"
      "int $3\n\t"
      SYMBOL(exam_nest) ":"
#  if JDK_VER < 12
      : : "m" (*(char **)vector[3])
#  else
      : : "m" (*((JITInterface6 *)jitinterface)->p_CompiledCodeSignalHandler)
#  endif
	// prevent elimination of the above assignment of the signal handler
	// gcc 3.4.2 eliminate it if there is no reference to the pointer
  );

#  if JDK_VER < 12
  *(char **)vector[3] = NULL;
#  else
  *((JITInterface6 *)jitinterface)->p_CompiledCodeSignalHandler = NULL;
#  endif

  if (sc_nest < 0) {
    printf("FATAL: cannot examine the offset of signal context.\n");
    JVM_Exit(1);
  }
#endif		// SIGNAL


  // set up link vector
#if JDK_VER < 12
  *(char **)vector[1] = (char *)initializeClassHook;
  *(char **)vector[2] = (char *)sym_invokeJITCompiledMethod;
  *(char **)vector[3] = (char *)signalHandler;
  *(char **)vector[4] = (char *)freeClass;
  *(char **)vector[5] = (char *)compileClass;
  *(char **)vector[6] = (char *)compileClasses;
#  ifndef IGNORE_DISABLE
  if (!OPT_SETQ(OPT_IGNDISABLE)) {
    *(char **)vector[7] = (char *)compilerEnable;
    *(char **)vector[8] = (char *)compilerDisable;
  }
#  endif	// IGNORE_DISABLE
  *(char **)vector[10] = (char *)pcInCompiledCode;
  *(char **)vector[11] = (char *)compiledCodePC;
#  ifdef DIRECT_INVOCATION
  *(char **)vector[70] = (char *)framePrev;
#  endif	// DIRECT_INVOCATION
#  ifdef COMPILE_DEBUG
  {
    int i;
    for (i = 1; i <= 8; i++)
      printf("*vector[%d]: 0x%08x\n", i, (int)*(char **)vector[i]);
    printf("*vector[10]: 0x%08x\n", i, (int)*(char **)vector[10]);
    printf("*vector[11]: 0x%08x\n", i, (int)*(char **)vector[11]);
    printf("*vector[70]: 0x%08x\n", i, (int)*(char **)vector[70]);
    fflush(stdout);
  }
#  endif	// COMPILE_DEBUG
#else	// JDK_VER
  {
    JITInterface6 *ji6 = (JITInterface6 *)jitinterface;

    *ji6->p_InitializeForCompiler = initializeClassHook;
    *ji6->p_invokeCompiledMethod = sym_invokeJITCompiledMethod;
    *ji6->p_CompiledCodeSignalHandler = signalHandler;
    *ji6->p_CompilerFreeClass = freeClass;

    *ji6->p_CompilerCompileClass = compileClass;
    *ji6->p_CompilerCompileClasses = compileClasses;
#ifndef IGNORE_DISABLE
    if (!OPT_SETQ(OPT_IGNDISABLE)) {
      *ji6->p_CompilerEnable = compilerEnable;
      *ji6->p_CompilerDisable = compilerDisable;
    }
#endif	// IGNORE_DISABLE

    *ji6->p_PCinCompiledCode = pcInCompiledCode;
    *ji6->p_CompiledCodePC = compiledCodePC;
#ifdef DIRECT_INVOCATION
    *ji6->p_CompiledFramePrev = framePrev;
#endif	// DIRECT_INVOCATION
    *ji6->p_CompiledFrameID = frameID;	// only for JDK >= 1.2
  }
#endif	// JDK_VER


#ifdef CODE_DB
  // open DB and page file
  if (OPT_SETQ(OPT_CODEDB)) {
    void *dl_dbm;
    if (!(dl_dbm = dlopen(LIBDBM, RTLD_LAZY))) {
      fputs(dlerror(), stderr);  fputc('\n', stderr);
      fprintf(stderr, "failed to open " LIBDBM ".\n");
      goto codedb_init_fail;
    }
#ifdef GDBM
    sym_dbm_open = (GDBM_FILE (*)(char *,int,int,int,void (*)()))
	dlsym(dl_dbm, "gdbm_open");
    sym_dbm_close = (void (*)(GDBM_FILE))dlsym(dl_dbm, "gdbm_close");
    sym_dbm_store = (int (*)(GDBM_FILE,datum,datum,int))
	dlsym(dl_dbm, "gdbm_store");
    sym_dbm_fetch = (datum (*)(GDBM_FILE,datum))dlsym(dl_dbm, "gdbm_fetch");
    sym_dbm_sync = (void (*)(GDBM_FILE))dlsym(dl_dbm, "gdbm_sync");
#else
    sym_dbm_open = (DBM *(*)(const char *,int,int))dlsym(dl_dbm, "dbm_open");
    sym_dbm_close = (void (*)(DBM *))dlsym(dl_dbm, "dbm_close");
    sym_dbm_store = (int (*)(DBM *,datum,datum,int))dlsym(dl_dbm, "dbm_store");
    sym_dbm_fetch = (datum (*)(DBM *,datum))dlsym(dl_dbm, "dbm_fetch");
#endif
    if (!(((int32_t)sym_dbm_open) && ((int32_t)sym_dbm_close) &&
	  ((int32_t)sym_dbm_store) && ((int32_t)sym_dbm_fetch))
#ifdef GDBM
	  && ((int32_t)sym_dbm_sync)
#endif
	) {
      fprintf(stderr, "cannot get symbols to handle DBM.\n");
      goto codedb_init_fail;
    }

    if ((db_page = open(CODEDB_PAGE,
		O_RDWR|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)) < 0) {
      perror("open");  goto codedb_init_fail;
    }

#ifdef GDBM
    if (!(db = sym_dbm_open(CODEDB_DB, 512,
	GDBM_WRCREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH, NULL))) {
      perror("gdbm_open");  goto codedb_init_fail;
    }
#else
    if (!(db = sym_dbm_open(CODEDB_DB, O_RDWR|O_CREAT, 0))) {
      perror("dbm_open");  goto codedb_init_fail;
    }
#endif

    goto codedb_init_done;
  codedb_init_fail:
    fprintf(stderr, "disable codedb.\n");  OPT_RESET(OPT_CODEDB);
    if (db_page >= 0)  close(db_page);
    JVM_Exit(1);
  }
codedb_init_done:
#endif	// CODE_DB


#if defined(EXC_BY_SIGNAL) && defined(COUNT_EXC_SIGNAL)
  // for printing the number of exceptions by signal
  atexit(showExcSignalCount);
#endif


  // for strictfp
  // check the FPU roundig precision
  {
    uint16_t cw;
    asm("fstcw %0" : "=m" (cw));
    is_fpupc_double = ((cw & 0x0300) == 0x0200);
#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG)
    printf("FPU rounding precision: %sdouble.\n",
	(is_fpupc_double?"":"not "));
#endif
  }


#ifdef METAVM
  // set remote flag on
  REMOTE_ADDR(ee) = NULL;
  REMOTE_FLAG_ON(ee);
#endif	// METAVM


#ifdef COMPILE_DEBUG
  printf("java_lang_Compiler_start() done.\n");
  fflush(stdout);
#endif
}


/*
 * Initialize the class at the time one is loaded.
 */
void initializeClassForJIT(ClassClass *cb,
	bool_t linkNative, bool_t initInvoker) {
  struct methodblock *mb;
  int mb_count;
  ExecEnv *ee = EE();

#ifdef METAVM
  // force a loaded class to implement java.io.Serializable
  {
    JNIEnv *env = EE2JNIEnv(ee);
    static ClassClass *clz_Serializable = NULL;
    if (!clz_Serializable) {
      jclass jclz_Ser = (*env)->FindClass(env, "java/io/Serializable");
      (*env)->NewGlobalRef(env, jclz_Ser);
      clz_Serializable = (ClassClass *)DeRef(env, jclz_Ser);
    }

    if (!cbIsInterface(cb) && (cb != classJavaLangObject))
      forceToImplement(ee, cb, clz_Serializable);
  }
#endif	// METAVM

  mb = cbMethods(cb);
  mb_count = cbMethodsCount(cb);
  for (; mb_count-- > 0; mb++) {
    int access = mb->fb.access;

    if (access & ACC_ABSTRACT)  continue;

    // initialize mb->CompiledCodeInfo
//    if (mb->CompiledCodeInfo == NULL)
      if (prepareCompiledCodeInfo(ee, mb) == NULL) {
	printf("FATAL: could not create CompiledCodeInfo for %s#%s %s.\n",
		cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
	JVM_Exit(1);
      }

    if (access & ACC_NATIVE) {
#if defined(METAVM) && (JDK_VER >= 12)
      if (linkNative) {
	// link native code
	void *code;
	LINKCLASS_LOCK(EE2SysThread(ee));
	code = mb->code;
	LINKCLASS_UNLOCK(EE2SysThread(ee));
	if (code == NULL) {
	  uint32_t isJNI;
	  char orig = GET_REMOTE_FLAG(ee);

	  REMOTE_FLAG_OFF(ee);
	  code = dynoLink(mb, &isJNI);	// link, which executes Java code
	  SET_REMOTE_FLAG(ee, orig);

	  if (code != NULL) {
	    LINKCLASS_LOCK(EE2SysThread(ee));
	    if (mb->code == NULL) {
	      mb->code = code;
	      if (isJNI) {	// JNI style
		if (mb->fb.access & ACC_SYNCHRONIZED)
		  mb->invoker = invokeJNISynchronizedNativeMethod;
		else {
		  Invoker inv = getCustomInvoker(methodTerseSig(mb));
		  if (inv)  mb->invoker = inv;
		  else  mb->invoker = invokeJNINativeMethod;
		}
	      }
	      else {		// old style
		mb->invoker = (mb->fb.access & ACC_SYNCHRONIZED) ?
			invokeSynchronizedNativeMethod : invokeNativeMethod;
	      }
	    }
	    LINKCLASS_UNLOCK(EE2SysThread(ee));
	  }
	}

#  ifdef COMPILE_DEBUG
	if (mb->code == NULL) {
	  // fail in linking
	  // because the native library has not been loaded yet.
	  printf("could not link now: %s#%s %s\n",
		cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
	  fflush(stdout);
	}
#  endif
      }	// if (linkNative)
#endif	// METAVM && JDK_VER >= 12
      continue;
    }

    if (!initInvoker)  continue;

    // initialize invoker
    if ((OPT_SETQ(OPT_CMPLCLINIT) || strcmp(mb->fb.name, "<clinit>")) &&
	(mb->CompiledCode == NULL)) {			// not compiled yet
		// assume that CompiledCode of not-yet-compiled method is NULL
      mb->invoker = sym_compileAndInvokeMethod;
    }
  }
}


//
// Local Functions
//

static void initializeClassHook(ClassClass *cb) {
#ifdef COMPILE_DEBUG
  printf("initializeClassHook(%s) called.\n", cbName(cb));
  fflush(stdout);
#endif

  initializeClassForJIT(cb, TRUE, TRUE);

  if (OPT_SETQ(OPT_CMPLATLOAD)) {	// have to done after initialization
    compileClass(cb);
  }

#ifdef COMPILE_DEBUG
  printf("initializeClassHook(%s) done.\n", cbName(cb));
  fflush(stdout);
#endif
}


/*
 * Freeing class stuffs related to the compiler.
 */
static void freeClass(ClassClass *cb) {
  struct methodblock *mb = cbMethods(cb);
  struct methodblock *mb_end = mb + cbMethodsCount(cb);

#ifdef COMPILE_DEBUG
  printf("freeClass() called.\n");
  fflush(stdout);
#endif

  for (; mb < mb_end; mb++)
    freeMethod(mb);
}


static unsigned char *compiledCodePC(JavaFrame *frame,struct methodblock *mb) {
#ifdef DIRECT_INVOCATION
  // in case of DIRECT_INVOCATION, frame->lastpc is not up-to-date.
  return mb->code;
#else
  return (frame ? frame->lastpc : mb->code);
#endif	// DIRECT_INVOCATION
}


static bool_t pcInCompiledCode(
	unsigned char *pc /* mb->lastpc */, struct methodblock *mb) {
#if 1	// mb->lastpc is a pc on bytecode
  unsigned int off = pc - mb->code;
  return (off < mb->code_length);
#else	// mb->lastpc is a pc on compiled code
  CodeInfo *info = (CodeInfo *)mb->CompiledCodeInfo;
  if (info) {
    unsigned char *compiled_code = (unsigned char *)mb->CompiledCode;
    unsigned char *limit = compiled_code + info->code_size;
    if (compiled_code != limit) {
      if ((compiled_code <= pc) && (pc < limit))  return TRUE;
    }
  }
  return FALSE;
#endif
}


#ifdef DIRECT_INVOCATION
#undef FRAMEPREV_DEBUG
static JavaFrame *framePrev(JavaFrame *frame, JavaFrame *frame_buf) {
  struct methodblock *mb = frame->current_method;
  uint32_t *basep;
  unsigned char *ret_addr;
  extern void invokeJIT_compiled_done(), candi_compiled_done();

#ifdef FRAMEPREV_DEBUG
  {
    printf("	| framePrev() called.\n");
    printf("	|   %s#%s %s\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
    fflush(stdout);
  }
#endif

  {
    unsigned char *lastpc = frame->lastpc;
    sysAssert(mb->code != NULL);
    if ((mb->code <= lastpc) && (lastpc < mb->code + mb->code_length)) {
      // this frame is for interpreter
#ifdef FRAMEPREV_DEBUG
      printf("	| this frame created by interpreter.\n");
      fflush(stdout);
#endif
      return frame->prev;
    }
  }

  basep = (uint32_t *)(frame->vars);
#ifdef FRAMEPREV_DEBUG
  printf("	| basep: %x\n", basep);
  fflush(stdout);
#endif

  if (frame != frame_buf) {	// first time
    memset((void *)frame_buf, 0, sizeof(JavaFrame));
    frame_buf->prev = frame->prev;
  }

  ret_addr = (unsigned char *)basep[1];
  if ((ret_addr == (unsigned char *)invokeJIT_compiled_done) ||
      (ret_addr == (unsigned char *)candi_compiled_done)) {
#ifdef FRAMEPREV_DEBUG
    printf("	| limit.\n\n");
    fflush(stdout);
#endif
    return frame->prev;
  }

  basep = (uint32_t *)basep[0];
  frame_buf->vars = (stack_item *)basep;
  frame_buf->current_method = (struct methodblock *)basep[3];
#ifdef FRAMEPREV_DEBUG
  {
    printf("	| method: %x\n", mb);
    fflush(stdout);
    printf("	|   buf->cur_method: %s#%s %s\n\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
    fflush(stdout);
  }
#endif
  return frame_buf;
}
#endif	// DIRECT_INVOCATION


#if JDK_VER >= 12
static void *frameID(JavaFrame *frame) {
  return (void *)frame->vars;
}
#endif



#ifndef IGNORE_DISABLE
/*
 * Enable and Disable the compiler.
 */
static void compilerEnable() {
#ifdef RUNTIME_DEBUG
  printf("compilerEnable() called.\n");  fflush(stdout);
#endif
  compiler_enabled = TRUE;
}

/*
 * Enable and Disable the compiler.
 */
static void compilerDisable() {
#ifdef RUNTIME_DEBUG
  printf("compilerDisable() called.\n");  fflush(stdout);
#endif
  compiler_enabled = FALSE;
}
#endif	// IGNORE_DISABLE


/*
 * Compile the class. Not only initialization.
 */
static bool_t compileClass(ClassClass *cb) {
  struct methodblock *mb, *mb_end;

#ifdef COMPILE_DEBUG
  printf("\n");
  printf("compileClass(%s) called.\n", cbName(cb));
  fflush(stdout);
#endif

#ifndef IGNORE_DISABLE
  if (!compiler_enabled) {
#ifdef COMPILE_DEBUG
    printf("  compiler has been disabled.\n  return.\n");
    fflush(stdout);
#endif
    return TRUE;
  }
#endif	// IGNORE_DISABLE

  mb = cbMethods(cb);
  mb_end = mb + cbMethodsCount(cb);
  for (; mb < mb_end; mb++) {
    int access = mb->fb.access;

    if (access & ACC_ABSTRACT)
      continue;

    // initialize mb->CompiledCodeInfo
    if (!(mb->CompiledCodeInfo))
      prepareCompiledCodeInfo(EE(), mb);

    if ((access & ACC_NATIVE) || !strcmp(mb->fb.name, "<clinit>"))
      continue;

    compileMethod(mb, STAGE_DONE);	// ignore the returned value
  }

#ifdef COMPILE_DEBUG
  printf("compileClass(%s) done.\n", cbName(cb));
  fflush(stdout);
#endif

  return TRUE;
}


/*
 * Compile the classes specified by comma-delimited names.
 */
static bool_t compileClasses(Hjava_lang_String *nameStr) {
  char *names = allocCString(nameStr);
  char *name = names;
  char *p = names;
  bool_t done = FALSE;

#ifdef COMPILE_DEBUG
  printf("compileClasses() called.\n");
  fflush(stdout);
#endif

  if (names) {
    ClassClass *cb;

    do {
      while ((*p != ',') && (*p != '\0'))  p++;
      if (*p == '\0')  done = TRUE;
      else  *p = '\0';

#ifdef COMPILE_DEBUG
      printf("compileClasses(): classname is `%s'\n", name);
      fflush(stdout);
#endif
      cb = FindClass(NULL, name, TRUE);
      if (cb)
	if (!(compileClass(cb)))  goto compileDone;

      p++;
      name = p;
    } while (!done);

  compileDone:
    sysFree(names);

    return TRUE;
  }

  return FALSE;
}


/*
 * Process the command from Java code.
 */
static HObject *compilerCommand(HObject *obj) {
  HObject *ret = NULL;
  
#ifdef COMPILE_DEBUG
  printf("compilerCommand() called.\n");
  fflush(stdout);
#endif

  // do nothing.

  return ret;
}
