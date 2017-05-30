/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1996,1997,1998,1999,2000,2001,2002,2003 Kazuyuki Shudo

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

#ifndef _COMPILER_H_
#define _COMPILER_H_


#define VERSION	"0.8.0"


#include "config.h"

// for {,u}int{8,16,32}_t types
#ifdef HAVE_STDINT_H
#  include <stdint.h>
#elif defined(__FreeBSD__) && defined(HAVE_SYS_TYPES_H)
#  include <sys/types.h>
#else
typedef unsigned char uint8_t;
#endif

#include "x86tsc.h"
#include "opcodes.h"
#include "opcodes_internal.h"

#ifdef METAVM
#  include "metavm/metavm.h"
	// constants.c requires prototype declarations
#endif	// METAVM


// offerred by Sun
#include "native.h"
#ifdef HPROF
#  if JDK_VER >= 12
#    include "vmprofiler.h"
#  endif
#endif


// Additional type definition

#if !(defined(_ILP32) || defined(_LP64) || defined(_STDINT_H) || defined(_SYS_INTTYPES_H_) /* for FreeBSD */)
#  ifndef _UINT16_T
#    define _UINT16_T
typedef unsigned short uint16_t;
#    endif	// _UINT16_T

#  ifndef __BIT_TYPES_DEFINED__
#    ifndef _INT16_T
#      define _INT16_T
typedef short int16_t;
#    endif	// _INT16_T
#  endif	// __BIT_TYPES_DEFINED__

#endif	// _ILP32, LP64

#ifdef __FreeBSD__
#  if __FreeBSD__ <= 2
typedef u_int16_t	uint16_t;
#  endif
#endif


// Additional macro definition
#define handleToClassClass(h)	(java_lang_Object_getClass((HObject *)(h)))


//
// macro definition
//

// feature control

#define PATCH_WITH_SIGTRAP
#undef PATCH_ON_JUMP
#undef INITCLASS_IN_COMPILATION

#define INVINTF_INLINE_CACHE
#define EXC_CHECK_IN_LOOP
#define METHOD_INLINING
#define DIRECT_INVOCATION
#define EAGER_COMPILATION
#define ELIMINATE_TAIL_RECURSION
#define SHORTEN_JUMP_INSN
#define CAUSE_STACKOVERFLOW
#define GET_SIGCONTEXT
#define NULLEXC_BY_SIGNAL
#define ARITHEXC_BY_SIGNAL
#define OPTIMIZE_INTERNAL_CODE
#define SPECIAL_INLINING
#define DIRECT_INV_NATIVE
#define ALIGN_JUMP_TARGET
#define OMIT_SCALING_SINGLE_PRECISION
// ignore invocation of java.lang.Compiler#disable()
#undef IGNORE_DISABLE
// allow access to private and protected method and field,
// and writing to final fields.
#undef SLACK_ACCESS_CONTROL
// omit null check and array bound check.
#undef NO_NULL_AND_ARRAY_CHECK
// at exit, print the number of exceptions by signal
#undef COUNT_EXC_SIGNAL
#undef COUNT_TSC


#ifdef _WIN32
#  undef PATCH_WITH_SIGTRAP
#  define PATCH_ON_JUMP

#  undef GET_SIGCONTEXT
#  undef NULLEXC_BY_SIGNAL
#  undef ARITHEXC_BY_SIGNAL

#  undef ALIGN_JUMP_TARGET
#endif


#define LAZY_COMPILATION_THRESHOLD_FOR_SYS	10
#define LAZY_COMPILATION_THRESHOLD_FOR_USER	1

#ifdef METHOD_INLINING
#  define METHOD_INLINING_MAXLEN	20
#  define METHOD_INLINING_DEPTH		2
#endif


#ifdef CAUSE_STACKOVERFLOW
#  if defined(linux)
#    define STACKOVERFLOW_MARGIN	200
#  elif defined(__FreeBSD__) || defined(__NetBSD__)
#    if JDK_VER >= 12
#      define STACKOVERFLOW_MARGIN	2700
#    else
#      define STACKOVERFLOW_MARGIN	3400
#    endif
#  else
#    define STACKOVERFLOW_MARGIN	200
#  endif
#endif

#ifdef PATCH_WITH_SIGTRAP
#  undef PATCH_ON_JUMP
#  undef INITCLASS_IN_COMPILATION
#elif defined(PATCH_ON_JUMP)
#  undef INITCLASS_IN_COMPILATION
#endif

#if defined(NULLEXC_BY_SIGNAL) || defined(ARITHEXC_BY_SIGNAL) || defined(PATCH_WITH_SIGTRAP)
#  define EXC_BY_SIGNAL
#endif

#ifdef METAVM
#  undef METHOD_INLINING
#  undef LAZY_COMPILATION_THRESHOLD_FOR_SYS
#  undef LAZY_COMPILATION_THRESHOLD_FOR_USER
#  define LAZY_COMPILATION_THRESHOLD_FOR_SYS	1
#  define LAZY_COMPILATION_THRESHOLD_FOR_USER	1
#endif

#ifndef DIRECT_INVOCATION
#  undef EAGER_COMPILATION
#endif

#ifndef NULLEXC_BY_SIGNAL
#  undef CAUSE_STACKOVERFLOW
#endif


// OS dependent macros

#if defined(linux)
#  define SEARCH_SIGCONTEXT
#elif defined(__FreeBSD__) || defined(__NetBSD__)
#  undef SEARCH_SIGCONTEXT
#else
#  undef SEARCH_SIGCONTEXT
#endif


#if defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)
#  if defined(linux)
#    define SIGCONTEXT_T struct sigcontext
#    define sigcontext_struct sigcontext
	// to permute if asm/sigcontext.h defines sigcontext_struct
#    include <asm/sigcontext.h>	// for struct sigcontext
#    include <linux/version.h>	// for kernel version
#  elif defined(__FreeBSD__) || defined(__NetBSD__)
#    ifdef __FreeBSD__
#      if __FreeBSD__ >= 4 && JDK_VER < 12
	// In this case, the 3rd arg. of sig. handler is not (sigcontext *).
#	 define SIGCONTEXT_T struct osigcontext
#      else
#	 define SIGCONTEXT_T struct sigcontext
#      endif
#      include <sys/signal.h>		// for sigset_t which sigcontext needs
#      include <machine/signal.h>	// for struct sigcontext
#    else	// NetBSD
#      define SIGCONTEXT_T struct sigcontext
#      include <sys/signal.h>	// for struct sigcontext
#    endif
#  else
#    define SIGCONTEXT_T struct sigcontext
#  endif
#endif	// EXC_BY_SIGNAL || GET_SIGCONTEXT


// OS independent macros

#ifdef CODE_DB
#  undef CODE_DB_DEBUG

#  define CODEDB_PREFIX		"shujit-code"
#  define CODEDB_DB_SUFFIX	".db"
#  define CODEDB_PAGE_SUFFIX	".page"

#  define CODEDB_PAGE	CODEDB_PREFIX CODEDB_PAGE_SUFFIX
#  ifdef GDBM
#    define LIBDBM	"libgdbm.so"
#    define CODEDB_DB	CODEDB_PREFIX CODEDB_DB_SUFFIX
#  else
#    define LIBDBM	"libndbm.so"
#    define CODEDB_DB	CODEDB_PREFIX
#  endif
#endif	// CODE_DB


#define OPC_THROW_MASK	0x1
#define OPC_SIGNAL_MASK	0x2
#define OPC_JUMP_MASK	0x4

#define OPC_NONE	0
#define OPC_THROW	0x1
	// the opcode may throw a throwable with SIGNAL_ERROR*()
#define OPC_SIGNAL	0x3
	// the opcode may send a signal: SIGSEGV or SIGFPE
#define OPC_JUMP	0x4


//
// Complement macros
//
#if 1
#  ifndef HAVE_GREENTHR_HEADER
#    define NATIVE	// for Linux/JDK 1.1.8v1
#  endif
#  ifdef _WIN32
#    define _JAVASOFT_WIN32_TIMEVAL_H_	// avoid redefinition of timercmp()
#    define _JAVASOFT_WIN32_IO_MD_H_	// avoid redefinition of S_IS*()
#  endif
#  include "sys_api.h"
#  undef NATIVE
#else
#  define sysMalloc	malloc
#  define sysFree	free
#  define sysCalloc	calloc
#  define sysRealloc	realloc
#endif
#if JDK_VER < 12 && !defined(sysThreadSelf)
#  define sysThreadSelf	threadSelf
#endif

#if JDK_VER < 12
#  define ACC_STRICT	0x0800 /* was ACC_XXUNUSED1 */	// strictfp
#  define monitorEnter2(EE, KEY)	monitorEnter(KEY)
#  define monitorExit2(EE, KEY)		monitorExit(KEY)
#  define JVM_LoadLibrary(NAME)	sysAddDLSegment(NAME)
#  define JVM_Exit(CODE)	sysExit(CODE)
#  define pExecuteJava		ExecuteJava
enum {
  TERSE_SIG_END = 0,
  TERSE_SIG_OBJECT, TERSE_SIG_LONG, TERSE_SIG_DOUBLE, TERSE_SIG_BOOLEAN,
  TERSE_SIG_BYTE, TERSE_SIG_SHORT, TERSE_SIG_CHAR, TERSE_SIG_INT,
  TERSE_SIG_FLOAT, TERSE_SIG_VOID, TERSE_SIG_ENDFUNC
};
#endif	// JDK_VER

#if JDK_VER >= 12
#  define SYS_MONITOR_ENTER(SELF, MON)	sysMonitorEnter(SELF, MON)
#  define SYS_MONITOR_EXIT(SELF, MON)	sysMonitorExit(SELF, MON)
#  define CB_INITIALIZED(CB)	(CCIs((CB), Initialized))
#else
#  define SYS_MONITOR_ENTER(SELF, MON)	sysMonitorEnter(MON)
#  define SYS_MONITOR_EXIT(SELF, MON)	sysMonitorExit(MON)
#  define CB_INITIALIZED(CB)	(CCIs((CB), Resolved))
#endif	// JDK_VER

#if 1
#  ifndef HAVE_GREENTHR_HEADER
#    define NATIVE	// for Linux/JDK 1.1.8v1
#  endif
#  include "monitor.h"	// for monitor{Enter,Exit}() and macro BINCLASS_*()
#  undef NATIVE
#else
typedef struct sys_mon sys_mon_t;
extern sys_mon_t *_binclass_lock;
#  if JDK_VER >= 12
#    define BINCLASS_LOCK(self)	sysMonitorEnter(self, _binclass_lock)
#    define BINCLASS_UNLOCK(self)	sysMonitorExit(self, _binclass_lock)
#  else
#    define BINCLASS_LOCK()	sysMonitorEnter(_binclass_lock)
#    define BINCLASS_UNLOCK()	sysMonitorExit(_binclass_lock)
#  endif	// JDK_VER
#endif

#if JDK_VER >= 12
#  define CODE_LOCK(self)	sysMonitorEnter(self, _code_lock)
#  define CODE_UNLOCK(self)	sysMonitorExit(self, _code_lock)
#endif



#ifdef METAVM
#  define JIT_LIB_NAME	"metavm"
#  define SYS_NAME	"MetaVM"
#else
#  define JIT_LIB_NAME	"shujit"
#  define SYS_NAME	"shuJIT"
#endif
#  define CREDIT "  " SYS_NAME "  for Sun Classic VM/x86  by Kazuyuki Shudo\n"

#if JDK_VER < 12
#  define COMPILER_VERSION	5
#else
#  define COMPILER_VERSION	6
#endif	// JDK_VER

#ifdef XBOX
#  define CODESIZE_FNAME	"Z:\\tmp\\jit_codesize"
#else
#  define CODESIZE_FNAME	"jit_codesize"
#endif


// STR(macro) is permuted to "value of the macro"
#define _STR(T)	#T
#define STR(MACRO)	_STR(MACRO)

#if (defined(__FreeBSD__) || defined(__NetBSD__)) && !defined(__ELF__)
#  define SYMBOL(SYM)	"_" STR(SYM)
#  define FUNCTION(SYM)	SYMBOL(SYM) "@PLT"
#elif defined(_WIN32)
#  define SYMBOL(SYM)	"_" STR(SYM)
#  define FUNCTION(SYM)	SYMBOL(SYM)
#else
#  define SYMBOL(SYM)	STR(SYM)
#  define FUNCTION(SYM)	SYMBOL(SYM) "@PLT"
#endif


#ifdef _WIN32
#  define CAST_INT8_TO_INT32(REG)   asm("shll $24," #REG "\n\tsarl $24," #REG);
#  define CAST_UINT8_TO_INT32(REG)  asm("andl $0xff," #REG);
#  define CAST_INT16_TO_INT32(REG)  asm("shll $16," #REG "\n\tsarl $16," #REG);
#  define CAST_UINT16_TO_INT32(REG) asm("andl $0xffff," #REG);
#else
#  define CAST_INT8_TO_INT32(REG)
#  define CAST_UINT8_TO_INT32(REG)
#  define CAST_INT16_TO_INT32(REG)
#  define CAST_UINT16_TO_INT32(REG)
#endif


//
// Type definition
//

// CatchFrame_w_state is based on CatchFrame
// Requirement for C compiler:
//	sizeof(CatchFrame_w_state) equals sizeof(CatchFrame)
typedef struct CatchFrame_w_state {
#if JDK_VER < 12
  long start_pc, end_pc;
  long handler_pc;
  void *compiled_CatchFrame;
  short catchType;
#else
  unsigned short start_pc, end_pc;
  unsigned short handler_pc;
  void *compiled_CatchFrame;
  unsigned short catchType;
#endif
  short state;	// added by SHUDO
} CatchFrame_w_state;


typedef struct pcentry {
  uint16_t opcode;
  uint16_t flag;
	// state: 4 bit, block head: 1 bit, loop head: 1 bit, loop tail: 1 bit
  int32_t operand;
  int32_t byteoff;		// in the method which the insn belonging to
	// can be less than 0, so should be signed
  int32_t increasing_byteoff;	// in the method constructed by inlining
  uint32_t nativeoff;
} pcentry;

typedef struct jpentry {
  unsigned int tgtoff;
  unsigned int argoff;
} jpentry;

typedef enum {
  STAGE_START = 0,
  STAGE_INTERNAL_CODE,
  STAGE_STATIC_PART,
  STAGE_DONE
} CompilationStage;

typedef struct compiler_context {
  ExecEnv *ee;
  struct methodblock *mb;
  int ref_count;

  CompilationStage stage;
  bool_t may_throw;	// the method may throw exceptions
  bool_t may_jump;

  // buffer for compiled code
#define DEFAULT_BUF_SIZE	8192
  unsigned char *buffer;
  int buf_size;
  unsigned char *bufp;

  // program counter table
#define DEFAULT_PCTABLE_SIZE	128
  int pctablesize;
  uint32_t pctablelen;
  uint32_t ninsn;
  pcentry *pctable;

  // jump instruction table
#define DEFAULT_JPTABLE_SIZE	128
  int jptablesize;
  int jptablelen;
  jpentry *jptable;

#ifdef COUNT_TSC
#define N_TSC	13
  unsigned long long int tsc[N_TSC];
#endif

  // to maintain a pool
  struct compiler_context *next;

#ifdef COMPILE_DEBUG
  int compile_debug;
#endif
} CompilerContext;


#if defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)
typedef struct throw_entry {
  uint32_t start;	// on generated native code
  uint16_t byteoff;
  uint8_t len;		// assume the length of native code <= 255
#  ifdef PATCH_WITH_SIGTRAP
  uint8_t patched_code;
  ClassClass *cb;
  uint16_t opcode;	// need 2 byte, because this is internal opcode
#  endif
} throwentry;
#endif	// EXC_BY_SIGNAL

typedef enum {
  INLINE_UNKNOWN = 0,
  INLINE_DONT,
  INLINE_MAY
} Inlineability;

typedef struct compiled_code_info {
  int32_t ret_size;
  char *argsizes;
  char *terse_sig;
  char *ret_sig;
#ifdef DIRECT_INV_NATIVE
  char *code;
	// old fasion native methods not wrapped by stub
#endif	// DIRECT_INV_NATIVE

  sys_mon_t *monitor;

  CompilerContext *cc;

#ifdef METHOD_INLINING
  // for inlining
  Inlineability inlineability;
  int pctablelen;
  pcentry *pctable;
#endif

  // specific to JIT compiled code
  uint32_t code_size;
  int32_t invocation_count;

  uint32_t exc_handler_nativeoff;
  uint32_t finish_return_nativeoff;

#ifdef EXC_BY_SIGNAL
#define INITIAL_THROWTABLE_SIZE	8
  throwentry *throwtable;
  uint32_t throwtablelen;
  uint32_t throwtablesize;
#endif	// EXC_BY_SIGNAL

#ifdef PATCH_WITH_SIGTRAP
  uint8_t *trampoline;
#endif	// PATCH_WITH_SIGTRAP
} CodeInfo;


//
// Global variables
//

// in compiler.c
extern sys_mon_t *global_monitor;

#if JDK_VER >= 12
extern bool_t executejava_in_asm;
#endif

#ifndef IGNORE_DISABLE
extern bool_t compiler_enabled;
#endif	// IGNORE_DISABLE

extern bool_t is_fpupc_double;
	// Is the FPU rounding precision double?

extern ClassClass *classJavaLangNoClassDefFoundError;
extern ClassClass *classJavaLangNoSuchFieldError;
extern ClassClass *classJavaLangNoSuchMethodError;

// in compile.c

extern unsigned char *compiledcode_min, *compiledcode_max;

// in signal.c
#if (defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)) && defined(SEARCH_SIGCONTEXT)
extern int sc_nest;
#endif

// in code.c
// for the precise floating-point semantics, i.e. strictfp
#undef STRICT_PRELOAD
	// It has a preload to preload scales into FPU register yet.
#undef STRICT_USE_FSCALE
#define STRICT_FSCALE_USE_FLOAT

#if 0
extern struct methodtable *object_methodtable;
	// for the macro OBJ_ARRAY_METHODTABLE in code.h
#endif

#ifdef METAVM
// in proxy.c
extern struct methodtable *proxy_methodtable;
#endif	// METAVM

#ifdef CODE_DB
#  ifdef GDBM
#    include <gdbm.h>
extern GDBM_FILE db;
#  else
#    include <ndbm.h>
extern DBM *db;
#  endif
extern int db_page;
#endif	// CODE_DB

#define OPT_SET(N) (options |= (1 << (N)))
#define OPT_RESET(N) (options & ~(1 << (N)))
#define OPT_SETQ(N) (options & (1 << (N)))
enum opt_bit {
  OPT_QUIET = 0,
	// suppress initial message and some outputs
  OPT_IGNDISABLE,
	// make java.lang.Compiler#disable() void
  OPT_OUTCODE,
	// write generated code to code_<classname>_<methodname>.s
  OPT_CODESIZE,
	// write code size of each methods to the file jit_codesize
  OPT_CMPLATLOAD,
	// compile the whole class when the class is loaded
  OPT_DONTCMPLVMCLZ,
	// suppress compilation classes
	// which is already loaded when JIT is initialized
  OPT_CMPLCLINIT,
	// compile class initializer
  OPT_IGNSTRICTFP,
	// ignore `strictfp' method modifier
  OPT_FRCSTRICTFP,
	// force `strictfp' semantics on every method
  OPT_FPSINGLE,
	// set rounding precision of FP arithmetics as `single precision'
  OPT_FPEXTENDED,
	// set rounding precision of FP arithmetics as `extended precision'
#ifdef HAVE_POSIX_SCHED
  OPT_SCHED_FIFO,
	// choose first in-first out scheduler
	// see sched_setscheduler(2)
	// POSIX.1b scheduling interface
  OPT_SCHED_RR,
	// choose round robin scheduler
#endif
  OPT_CODEDB,
	// save and re-use generated native code
  OPT_IGNLOCK
	// do not handle monitor
};
extern int options;
extern int opt_systhreshold;
extern int opt_userthreshold;
#ifdef METHOD_INLINING
extern int opt_inlining_maxlen;
extern int opt_inlining_depth;
#endif

extern void *sym_compileAndInvokeMethod;
extern void *sym_invokeJITCompiledMethod;
extern void *sym_invokeJavaMethod;
extern void *sym_invokeSynchronizedJavaMethod;
extern void *sym_invokeAbstractMethod;
extern void *sym_invokeNativeMethod;
extern void *sym_invokeSynchronizedNativeMethod;
extern void *sym_invokeJNINativeMethod;
extern void *sym_invokeJNISynchronizedNativeMethod;
extern void *sym_invokeLazyNativeMethod;
#if JDK_VER >= 12
extern uint32_t sym_invokeJNI_min, sym_invokeJNI_max;
#endif
#ifdef CODE_DB
#  ifdef GDBM
extern GDBM_FILE (*sym_dbm_open)(char *,int,int,int,void (*)());
extern void (*sym_dbm_close)(GDBM_FILE);
extern int (*sym_dbm_store)(GDBM_FILE,datum,datum,int);
extern datum (*sym_dbm_fetch)(GDBM_FILE,datum);
extern void (*sym_dbm_sync)(GDBM_FILE);
#  else
extern DBM *(*sym_dbm_open)(const char *,int,int);
extern void (*sym_dbm_close)(DBM *);
extern int (*sym_dbm_store)(DBM *,datum,datum,int);
extern datum (*sym_dbm_fetch)(DBM *,datum);
#  endif
#endif	// CODE_DB


//
// Global functions
//
// in compiler.c
void initializeClassForJIT(ClassClass *,
	bool_t linkNative, bool_t initInvoker);

// in signal.c
#if (defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)) && defined(SEARCH_SIGCONTEXT)
bool_t examineSigcontextNestCount(int sig, void *info, void *uc0);
#endif
bool_t signalHandler(int sig, void *info, void *uc0);
#if defined(EXC_BY_SIGNAL) && defined(COUNT_EXC_SIGNAL)
void showExcSignalCount();
#endif

// in code.c
extern volatile void assembledCode(
	JHandle *o, struct methodblock *mb, int args_size, ExecEnv *ee,
	stack_item *var_base
#ifdef RUNTIME_DEBUG
	, int runtime_debug
#endif
);

extern void exceptionHandlerWrapper(
	JHandle *o, struct methodblock *mb, int args_size, ExecEnv *ee,
	stack_item *var_base
#ifdef RUNTIME_DEBUG
	, int runtime_debug
#endif
);
extern void exceptionHandler(void);

// in linker.c
#if JDK_VER >= 12
extern void *symbolInSystemClassLoader(char *name);
#else
#  define symbolInSystemClassLoader(NAME)	sysDynamicLink(NAME)
#endif	// JDK_VER

// in computil.c
extern void *access2invoker(int access);
extern char *nameOfInvoker(void *inv);

extern void showCompilerContext(CompilerContext *cc, char *prefix);
extern CompilerContext *getCompilerContext(struct methodblock *mb);
extern void releaseCompilerContext(CompilerContext *cc);
extern inline void ensureBufferSize(CompilerContext *cc, size_t req);
extern void writeToBuffer(CompilerContext *cc, void *, size_t);

#ifdef CODE_DB
extern void pctableExtend(CompilerContext *cc, uint32_t size);
#endif	// CODE_DB
extern void pctableClear(CompilerContext *cc);
extern uint32_t pctableLen(CompilerContext *cc);
extern void pctableSetLen(CompilerContext *cc, uint32_t len);
extern void pctableAdd(CompilerContext *cc,
	int opcode, int operand, unsigned int byteoff);
extern void pctableNInsert(CompilerContext *cc, int index,
	pcentry *srcentry, int srclen);
extern pcentry *pctableInsert(CompilerContext *cc, int index,
	int opcode, int operand, int32_t byteoff,
	int state, int nativeoff);
extern void pctableNDelete(CompilerContext *cc, int index, int len);
extern void pctableDelete(CompilerContext *cc, int index);
extern pcentry *pctableNext(CompilerContext *cc, pcentry *entry);
extern pcentry *pctableGet(CompilerContext *cc, int index);
extern pcentry *pctableGetByPC(CompilerContext *cc, int32_t byteoff);
extern void pcentryClear(pcentry *entry);
#define pcentryState(PCENTRY)		(((PCENTRY)->flag) & 0xf)
#define pcentrySetState(PCENTRY, ST) \
	((PCENTRY)->flag &= ~0xf); ((PCENTRY)->flag |= (ST & 0xf))
#define pcentryBlockHead(PCENTRY)	(((PCENTRY)->flag >> 4) & 0x1)
#define pcentrySetBlockHead(PCENTRY)	((PCENTRY)->flag |= 0x10)
#define pcentryClearBlockHead(PCENTRY)	((PCENTRY)->flag &= ~0x10)
#define pcentryLoopHead(PCENTRY)	(((PCENTRY)->flag >> 5) & 0x1)
#define pcentrySetLoopHead(PCENTRY)	((PCENTRY)->flag |= 0x20)
#define pcentryClearLoopHead(PCENTRY)	((PCENTRY)->flag &= ~0x20)
#define pcentryLoopTail(PCENTRY)	(((PCENTRY)->flag >> 6) & 0x1)
#define pcentrySetLoopTail(PCENTRY)	((PCENTRY)->flag |= 0x40)
#define pcentryClearLoopTail(PCENTRY)	((PCENTRY)->flag &= ~0x40)

extern void jptableAdd(CompilerContext *cc,
	unsigned int tgtoff, unsigned int argoff);

extern CodeInfo *prepareCompiledCodeInfo(ExecEnv *ee, struct methodblock *mb);
extern void freeCompiledCodeInfo(CodeInfo *info);

#ifdef EXC_BY_SIGNAL
#ifdef CODE_DB
extern void throwtableExtend(CodeInfo *info, uint32_t size);
#endif	// CODE_DB
extern throwentry *throwtableAdd(CompilerContext *cc, CodeInfo *info,
	uint32_t start, uint8_t len, uint16_t byteoff);
extern throwentry *throwtableGet(CodeInfo *info, uint32_t nativeoff);
#endif	// EXC_BY_SIGNAL

// in invoker.c
extern bool_t compileAndInvokeMethod(
	JHandle *o, struct methodblock *mb, int args_size, ExecEnv *ee,
	stack_item *var_base);
extern bool_t invokeJITCompiledMethod(
	JHandle *o, struct methodblock *mb, int args_size, ExecEnv *ee,
	stack_item *var_base_read_only);
#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG)
extern bool_t debugp(struct methodblock *mb);
#endif

// in compile.c
extern int compileMethod(struct methodblock *mb,
				CompilationStage target_stage);
extern void freeMethod(struct methodblock *mb);

#define ALIGNUP32(n)	( ((unsigned int)(n) + 3) & ~((unsigned int)3) )

#define GET_UINT16(p)	((((unsigned char *)(p))[0] << 8) | (p)[1])
#define GET_INT16(p)	((((signed char *)(p))[0] << 8) | (p)[1])
#define GET_INT32(p)\
 ((int)((unsigned char *)(p))[0] << 24| ((int)((unsigned char *)(p))[1] << 16) | ((int)((unsigned char *)(p))[2] << 8) | ((int)((unsigned char *)(p))[3]))

// in optimize.c
extern void peepholeOptimization(CompilerContext *cc);
extern void methodInlining(CompilerContext *cc);
extern void eagerCompilation(CompilerContext *cc);

#ifdef CODE_DB
// in codedb.c
void writeCompiledCode(
#  ifdef GDBM
GDBM_FILE db
#  else
DBM *db
#  endif
, int fd, CompilerContext *cc);
int readCompiledCode(
#  ifdef GDBM
GDBM_FILE db
#  else
DBM *db
#  endif
, int fd, CompilerContext *cc);
#endif	// CODE_DB

// in runtime.c
extern void invocationHelper(
	JHandle *obj, struct methodblock *method, int args_size, ExecEnv *ee,
	stack_item *var_base
#ifdef RUNTIME_DEBUG
	, int runtime_debug
#endif
);
extern struct methodblock *getInterfaceMethod(
	JHandle *obj, ExecEnv *ee,
	struct methodblock *imethod, unsigned char *guessptr
#ifdef INVINTF_INLINE_CACHE
	, unsigned char *cache_ptr
#endif
#ifdef RUNTIME_DEBUG
	, int runtime_debug
#endif
);
extern JHandle *multianewarray(
#ifdef RUNTIME_DEBUG
	int runtime_debug,
#endif
	ExecEnv *ee, int dimensions, ClassClass *arrayclazz,
	stack_item *stackpointer);
extern struct CatchFrame *searchCatchFrame(ExecEnv *ee, struct methodblock *mb,
	int bytepcoff
#ifdef RUNTIME_DEBUG
	, int runtime_debug
#endif
);
#if !defined(INITCLASS_IN_COMPILATION) && !defined(NO_PATCH)
int once_InitClass(ExecEnv *ee, ClassClass *cb);
#endif
#if JDK_VER < 12
extern void InitClass(ClassClass *cb);
	// wrapper must be a function, not a macro
#endif
extern void showStackFrames(ExecEnv *ee);
#ifdef RUNTIME_DEBUG
extern void showArguments(struct methodblock *mb, stack_item *vars);
#endif
#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG)
extern void showExcStackTrace(JHandle *throwable);
#endif
#if defined(RUNTIME_DEBUG) || defined(COMPILE_DEBUG) || defined(EXC_BY_SIGNAL) || defined(GET_SIGCONTEXT)
extern struct methodblock *methodByPC(unsigned char *pc);
#endif
#ifdef RUNTIME_DEBUG
extern char *showObjectBody(char *sig, JHandle *obj);
#endif


#if JDK_VER >= 12
#  define EXPAND_JAVASTACK(EE, STACK, FRAME, OPTOP,\
		ARGS_SIZE, NLOCAL, CAPACITY) \
    if (FRAME->ostack + CAPACITY >= STACK->end_data) {\
      JavaStack *tmp_stack = STACK;\
      JavaFrame *tmp_frame = FRAME;\
      stack_item *tmp_optop = OPTOP;\
      if (!ExpandJavaStack(EE, &tmp_stack, &tmp_frame, &tmp_optop,\
			ARGS_SIZE, NLOCAL, CAPACITY))\
	return FALSE;\
      STACK = tmp_stack;\
      FRAME = tmp_frame;\
      OPTOP = tmp_optop;\
    }
#  define EXPAND_JAVASTACK_FOR_NATIVE(EE, STACK, FRAME) \
    if (FRAME->ostack + JNI_REF_INFO_SIZE + JNI_DEFAULT_LOCAL_CAPACITY >=\
	STACK->end_data) {\
      JavaStack *tmp_stack = stack;\
      JavaFrame *tmp_frame = frame;\
      if (!ExpandJavaStackForJNI(EE, &tmp_stack, &tmp_frame,\
			JNI_REF_INFO_SIZE + JNI_DEFAULT_LOCAL_CAPACITY))\
        return FALSE;\
      STACK = tmp_stack;\
      FRAME = tmp_frame;\
    }
#else
#  define EXPAND_JAVASTACK(EE, STACK, FRAME, OPTOP,\
		ARGS_SIZE, NLOCAL, CAPACITY) \
    if (FRAME->ostack + CAPACITY >= stack->end_data) {\
      if (STACK->next)  STACK = STACK->next;\
      else {\
	if (STACK->stack_so_far + JAVASTACK_CHUNK_SIZE * sizeof(stack_item)\
		> JavaStackSize) {\
	  SignalError(EE, JAVAPKG "StackOverflowError", 0);\
	  return FALSE;\
	}\
	if (!(STACK = CreateNewJavaStack(EE, STACK))) {\
	  SignalError(EE, JAVAPKG "OutOfMemoryError", 0);\
	  return FALSE;\
	}\
      }\
      frame = (JavaFrame *)(STACK->data + NLOCAL);\
	/* needless to copy args to a frame on new stack chunk */\
    }
#  define EXPAND_JAVASTACK_FOR_NATIVE(EE, STACK, FRAME) \
    EXPAND_JAVASTACK(EE, STACK, FRAME, 0, 0, 0, 0)
#endif	// JDK_VER


#define CREATE_JAVAFRAME_0(EE, MB, OLD_FRAME, NEW_FRAME,\
  TENTATIVE_FRAME_OFFSET /* Java method: nlocal, Native method: args_size */,\
	ARGS_SIZE, NLOCAL)\
  {\
    JavaStack *stack = OLD_FRAME->javastack;\
    stack_item *optop = OLD_FRAME->optop;\
    \
    NEW_FRAME = (JavaFrame *)(optop + (TENTATIVE_FRAME_OFFSET))

#define CREATE_JAVAFRAME_1(EE, MB, OLD_FRAME, NEW_FRAME) \
    NEW_FRAME->javastack = stack;\
    NEW_FRAME->prev = OLD_FRAME;\
    NEW_FRAME->vars = optop;\
    NEW_FRAME->optop = NEW_FRAME->ostack;\
    NEW_FRAME->current_method = MB;\
    /* NEW_FRAME->constant_pool = cbConstantPool(fieldclass(&MB->fb)); */\
    NEW_FRAME->returnpc = NEW_FRAME->lastpc = MB->code;\
	/* lastpc is not initialized in invoke*JavaMethod() */\
    \
    EE->current_frame = NEW_FRAME;\
  }	// create a new frame


#define CREATE_JAVAFRAME(EE, MB, OLD_FRAME, NEW_FRAME,\
			ARGS_SIZE, NLOCAL, CAPACITY) \
  CREATE_JAVAFRAME_0(EE, MB, OLD_FRAME, NEW_FRAME, NLOCAL, ARGS_SIZE, NLOCAL);\
  EXPAND_JAVASTACK(EE, stack, NEW_FRAME, optop, ARGS_SIZE, NLOCAL, CAPACITY);\
  CREATE_JAVAFRAME_1(EE, MB, OLD_FRAME, NEW_FRAME)

#if JDK_VER >= 12
#  define CREATE_JAVAFRAME_FOR_NATIVE(EE, MB, OLD_FRAME, NEW_FRAME, ARGS_SIZE)\
  CREATE_JAVAFRAME_0(EE, MB, OLD_FRAME, NEW_FRAME, ARGS_SIZE, ARGS_SIZE, 0);\
  EXPAND_JAVASTACK_FOR_NATIVE(EE, stack, NEW_FRAME);\
  CREATE_JAVAFRAME_1(EE, MB, OLD_FRAME, NEW_FRAME);\
  \
  NEW_FRAME->optop += JNI_REF_INFO_SIZE;\
  JNI_REFS_FREELIST(NEW_FRAME) = NULL;\
  JNI_N_REFS_IN_USE(NEW_FRAME) = 0;\
  JNI_REFS_CAPACITY(NEW_FRAME) = JNI_DEFAULT_LOCAL_CAPACITY
#else
#  define CREATE_JAVAFRAME_FOR_NATIVE(EE, MB, OLD_FRAME, NEW_FRAME, ARGS_SIZE)\
  CREATE_JAVAFRAME_0(EE, MB, OLD_FRAME, NEW_FRAME, ARGS_SIZE, ARGS_SIZE, 0);\
  EXPAND_JAVASTACK_FOR_NATIVE(EE, stack, NEW_FRAME);\
  CREATE_JAVAFRAME_1(EE, MB, OLD_FRAME, NEW_FRAME)
#endif	// JDK_VER


#endif // _COMPILER_H_
