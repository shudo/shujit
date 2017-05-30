/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1998,1999,2000,2001,2002,2003,2005 Kazuyuki Shudo

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

#include "code.h"

#ifdef METAVM
#include "metavm/metavm.h"
#include "metavm/NET_shudo_metavm_Proxy_old.h"	// for type H...Proxy
#include "metavm/NET_shudo_metavm_VMAddress_old.h"// for type H...VMAddress
#endif	// METAVM


//
// Global Variables
//

// an array of signal names
static const char *signal_name[] = {	// EXCID
#define EXCID_NullPointerException	0
  JAVAPKG "NullPointerException",
#define EXCID_OutOfMemoryError		1
  JAVAPKG "OutOfMemoryError",
#define EXCID_ClassCastException	2
  JAVAPKG "ClassCastException",
#define EXCID_IllegalAccessError	3
  JAVAPKG "IllegalAccessError",
#define EXCID_InstantiationError	4
  JAVAPKG "InstantiationError",
#define EXCID_NoClassDefFoundError	5
  JAVAPKG "NoClassDefFoundError",
#define EXCID_NoSuchFieldError		6
  JAVAPKG "NoSuchFieldError",
#define EXCID_NoSuchMethodError		7
  JAVAPKG "NoSuchMethodError",
#define EXCID_AbstractMethodError	8
  JAVAPKG "AbstractMethodError",
#define EXCID_ArithmeticException	9
  JAVAPKG "ArithmeticException",
#define EXCID_ArrayIndexOutOfBoundsException	10
  JAVAPKG "ArrayIndexOutOfBoundsException",
#define EXCID_NegativeArraySizeException	11
  JAVAPKG "NegativeArraySizeException",
#define EXCID_ArrayStoreException	12
  JAVAPKG "ArrayStoreException",
#define EXCID_IOException	13
  JAVAIOPKG "IOException"
};

// for strictfp
#ifndef USE_SSE2
#  ifdef STRICT_USE_FSCALE
#    ifdef STRICT_FSCALE_USE_FLOAT
// exponents of scales
float double_scale_pos = 15360.0f;	//   16383 - 1023
float double_scale_neg = -15360.0f;	// -(16383 - 1023)
float single_scale_pos = 16256.0f;	//   16383 - 127
float single_scale_neg = -16256.0f;	// -(16383 - 127)
#    else
  // scales in integer
int32_t double_scale_pos = 15360;
int32_t double_scale_neg = -15360;
int32_t single_scale_pos = 16256;
int32_t single_scale_neg = -16256;
#    endif	// STRICT_FSCALE_USE_FLOAT
#  else
  // scales in extended precision floating-point
unsigned const char double_scale_pos[10] =
	{ 0, 0, 0, 0, 0, 0, 0, 0x80, 0xff, 0x7b };  // 2^  (16383 - 1023)
unsigned const char double_scale_neg[10] =
	{ 0, 0, 0, 0, 0, 0, 0, 0x80, 0xff, 0x03 };  // 2^ -(16383 - 1023)
unsigned const char single_scale_pos[10] =
	{ 0, 0, 0, 0, 0, 0, 0, 0x80, 0x7f, 0x7f };  // 2^  (16383 - 127)
unsigned const char single_scale_neg[10] =
	{ 0, 0, 0, 0, 0, 0, 0, 0x80, 0x7f, 0x00 };  // 2^ -(16383 - 127)
#  endif	// STRICT_USE_FSCALE
#endif	// !USE_SSE2


#define COMPILEDCODE(DST) \
    /* DST = mb->CompiledCode */\
    asm("movl  %0,%" #DST : : "m" (mb));\
    asm("movl  " METHOD_COMPILEDCODE(DST) "," #DST)


volatile void assembledCode(
	JHandle *o /* 8(%ebp) */ , struct methodblock *mb /* 12(%ebp) */,
	int args_size, ExecEnv *ee, stack_item *var_base
#ifdef RUNTIME_DEBUG
	, int runtime_debug /* 1c(%ebp) */
#endif
) {
  // int32_t bytepcoff;		// -4(%ebp)
	// for handling exceptions
  // uint16_t preserved_fpucw;	// -6(%ebp)
  // uint16_t fpucw;		// -8(%ebp)

  //
  // %esi: stack_item *vars
  //


  //
  // utilities
  //
#ifdef RUNTIME_DEBUG
#  define CLAZZ_DEBUG(CB) \
  asm("pushl %eax");\
  if (runtime_debug) {\
    DEBUG_IN;\
    CB_NAME(CB, %eax);  asm("pushl %eax");\
    PUSH_CONSTSTR("  clazz: %s\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $8,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#  define METHOD_DEBUG(MB, LABEL) \
  asm("pushl %eax");\
  if (runtime_debug) {		/* break eax */ \
    asm("movl  (%esp),%eax");	/* restore */ \
    DEBUG_IN;\
    asm("movl  " METHOD_NAME(MB) ",%edi\n\t"\
	"testl %edi,%edi\n\t"\
	"jz    " LABEL "_done\n\t"\
	"pushl %edi");\
    asm("movl  " METHOD_CLAZZ(MB) ",%edi");\
    CB_NAME(%edi, %edi);\
    asm("pushl %edi");\
    PUSH_CONSTSTR("  %s#%s\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $12,%esp");\
    FFLUSH;\
    asm(LABEL "_done:");\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#  define OBJ_DEBUG(OBJ) \
  asm("pushl %eax");\
  if (runtime_debug) {		/* break eax */ \
    asm("movl  (%esp),%eax");	/* restore */ \
    DEBUG_IN;\
    OBJ_METHODTABLE(OBJ, %edi);\
    /*MT_CLASSDESCRIPTOR(%edi, %edi);*/\
    /*CB_NAME(%edi, %edi);*/\
    asm("pushl %edi");\
    asm("pushl " #OBJ);\
    PUSH_CONSTSTR("  obj: 0x%08x mt: 0x%08x\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $12,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#  define VALUE_DEBUG(REG) \
  asm("pushl %eax");\
  if (runtime_debug) {		/* break eax */ \
    asm("movl  (%esp),%eax");	/* restore */ \
    DEBUG_IN;\
    asm("pushl " #REG "\n\t"\
	"flds  (%esp)\n\t"\
	"subl  $4,%esp\n\t"\
	"fstpl (%esp)\n\t"\
	"pushl " #REG "\n\t"\
	"pushl " #REG);\
    PUSH_CONSTSTR("  0x%08x %d %g\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $20,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#  define VALUE64_DEBUG(LREG, HREG) \
  asm("pushl %eax");\
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl " #HREG "\n\tpushl " #LREG "\n\t"\
	"fldl  (%esp)\n\tfstpl (%esp)\n\t"\
	"pushl " #HREG "\n\tpushl " #LREG "\n\t"\
	"pushl " #HREG "\n\tpushl " #LREG);\
    PUSH_CONSTSTR("  0x%016llx %lld %g\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $28,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#else
#  define CLAZZ_DEBUG(CB)
#  define METHOD_DEBUG(MB, LABEL)
#  define OBJ_DEBUG(OBJ)
#  define VALUE_DEBUG(REG)
#  define VALUE64_DEBUG(LREG, HREG)
#endif


  // fill_cache
#ifdef RUNTIME_DEBUG
#  define FILL_CACHE_DEBUG1(OPTOP1_REG, OPTOP2_REG) \
  asm("pushl %eax");\
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl " #OPTOP2_REG "\n\tflds  (%esp)\n\t"\
	"subl  $4,%esp\n\tfstpl (%esp)\n\t"\
	"pushl " #OPTOP2_REG "\n\tpushl " #OPTOP2_REG);\
    asm("pushl " #OPTOP1_REG "\n\tflds  (%esp)\n\t"\
	"subl  $4,%esp\n\tfstpl (%esp)\n\t"\
	"pushl " #OPTOP1_REG "\n\tpushl " #OPTOP1_REG);\
    PUSH_CONSTSTR("  optop[-1]: 0x%08x %d %g, optop[-2]: 0x%08x %d %g\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $36,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#else
#  define FILL_CACHE_DEBUG1(OPTOP1_REG, OPTOP2_REG)
#endif

  CODE(opc_fill_cache, fill_cache, ST0, ST2, OPC_NONE) {
    asm("popl  %ecx\n\tpopl  %edx");
    FILL_CACHE_DEBUG1(%ecx, %edx);
  }
  CODE(opc_fill_cache, fill_cache, ST1, ST4, OPC_NONE) {
    asm("popl  %ecx");
    FILL_CACHE_DEBUG1(%edx, %ecx);
  }
  CODE(opc_fill_cache, fill_cache, ST2, ST2, OPC_NONE) {
    FILL_CACHE_DEBUG1(%ecx, %edx);
  }
  CODE(opc_fill_cache, fill_cache, ST3, ST2, OPC_NONE) {
    asm("popl  %edx");
    FILL_CACHE_DEBUG1(%ecx, %edx);
  }
  CODE(opc_fill_cache, fill_cache, ST4, ST4, OPC_NONE) {
    FILL_CACHE_DEBUG1(%edx, %ecx);
  }

  CODE(opc_flush_cache, flush_cache, ST0, ST0, OPC_NONE) {
  }
  CODE(opc_flush_cache, flush_cache, ST1, ST0, OPC_NONE) {
    asm("pushl %edx");
  }
  CODE(opc_flush_cache, flush_cache, ST2, ST0, OPC_NONE) {
    asm("pushl %edx\n\tpushl %ecx");
  }
  CODE(opc_flush_cache, flush_cache, ST3, ST0, OPC_NONE) {
    asm("pushl %ecx");
  }
  CODE(opc_flush_cache, flush_cache, ST4, ST0, OPC_NONE) {
    asm("pushl %ecx\n\tpushl %edx");
  }


  // to another state
  CODE(opc_stateto0, stateto00, ST0, ST0, OPC_NONE) {}
  CODE(opc_stateto0, stateto10, ST1, ST0, OPC_NONE) { asm("pushl %edx"); }
  CODE(opc_stateto0, stateto20, ST2, ST0, OPC_NONE) {
    asm("pushl %edx\n\t"
	"pushl %ecx"); }
  CODE(opc_stateto0, stateto30, ST3, ST0, OPC_NONE) { asm("pushl %ecx"); }
  CODE(opc_stateto0, stateto40, ST4, ST0, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"pushl %edx"); }
  CODE(opc_stateto1, stateto01, ST0, ST1, OPC_NONE) { asm("popl  %edx"); }
  CODE(opc_stateto1, stateto11, ST1, ST1, OPC_NONE) {}
  CODE(opc_stateto1, stateto21, ST2, ST1, OPC_NONE) {
    asm("pushl %edx\n\t"
	"movl  %ecx,%edx"); }
  CODE(opc_stateto1, stateto31, ST3, ST1, OPC_NONE) { asm("movl  %ecx,%edx"); }
  CODE(opc_stateto1, stateto41, ST4, ST1, OPC_NONE) { asm("pushl %ecx"); }
  CODE(opc_stateto2, stateto02, ST0, ST2, OPC_NONE) {
    asm("popl  %ecx\n\t"
	"popl  %edx"); }
  CODE(opc_stateto2, stateto12, ST1, ST2, OPC_NONE) {
    asm("movl  %edx,%ecx\n\t"
	"popl  %edx"); }
  CODE(opc_stateto2, stateto22, ST2, ST2, OPC_NONE) {}
  CODE(opc_stateto2, stateto32, ST3, ST2, OPC_NONE) { asm("popl  %edx"); }
  CODE(opc_stateto2, stateto42, ST4, ST2, OPC_NONE) { asm("xchg  %edx,%ecx"); }
  CODE(opc_stateto3, stateto03, ST0, ST3, OPC_NONE) { asm("popl  %ecx"); }
  CODE(opc_stateto3, stateto13, ST1, ST3, OPC_NONE) { asm("movl  %edx,%ecx"); }
  CODE(opc_stateto3, stateto23, ST2, ST3, OPC_NONE) { asm("pushl %edx"); }
  CODE(opc_stateto3, stateto33, ST3, ST3, OPC_NONE) {}
  CODE(opc_stateto3, stateto43, ST4, ST3, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"movl  %edx,%ecx"); }
  CODE(opc_stateto4, stateto04, ST0, ST4, OPC_NONE) {
    asm("popl  %edx\n\t"
	"popl  %ecx"); }
  CODE(opc_stateto4, stateto14, ST1, ST4, OPC_NONE) { asm("popl  %ecx"); }
  CODE(opc_stateto4, stateto24, ST2, ST4, OPC_NONE) { asm("xchg  %edx,%ecx"); }
  CODE(opc_stateto4, stateto34, ST3, ST4, OPC_NONE) {
    asm("movl  %ecx,%edx\n\t"
	"popl  %ecx"); }
  CODE(opc_stateto4, stateto44, ST4, ST4, OPC_NONE) {}


  // to another state and jump
#define JUMP_TO_ACCUMULATOR	asm("jmp   *%eax")

  CODE(opc_goto_st0, goto_st00, ST0, ST0, OPC_NONE) {
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st0, goto_st10, ST1, ST0, OPC_NONE) {
    asm("pushl %edx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st0, goto_st20, ST2, ST0, OPC_NONE) {
    asm("pushl %edx\n\t"
	"pushl %ecx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st0, goto_st30, ST3, ST0, OPC_NONE) {
    asm("pushl %ecx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st0, goto_st40, ST4, ST0, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"pushl %edx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st1, goto_st01, ST0, ST1, OPC_NONE) {
    asm("popl  %edx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st1, goto_st11, ST1, ST1, OPC_NONE) {
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st1, goto_st21, ST2, ST1, OPC_NONE) {
    asm("pushl %edx\n\t"
	"movl  %ecx,%edx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st1, goto_st31, ST3, ST1, OPC_NONE) {
    asm("movl  %ecx,%edx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st1, goto_st41, ST4, ST1, OPC_NONE) {
    asm("pushl %ecx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st2, goto_st02, ST0, ST2, OPC_NONE) {
    asm("popl  %ecx\n\t"
	"popl  %edx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st2, goto_st12, ST1, ST2, OPC_NONE) {
    asm("movl  %edx,%ecx\n\t"
	"popl  %edx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st2, goto_st22, ST2, ST2, OPC_NONE) {
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st2, goto_st32, ST3, ST2, OPC_NONE) {
    asm("popl  %edx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st2, goto_st42, ST4, ST2, OPC_NONE) {
    asm("xchg  %edx,%ecx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st3, goto_st03, ST0, ST3, OPC_NONE) {
    asm("popl  %ecx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st3, goto_st13, ST1, ST3, OPC_NONE) {
    asm("movl  %edx,%ecx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st3, goto_st23, ST2, ST3, OPC_NONE) {
    asm("pushl %edx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st3, goto_st33, ST3, ST3, OPC_NONE) {
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st3, goto_st43, ST4, ST3, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"movl  %edx,%ecx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st4, goto_st04, ST0, ST4, OPC_NONE) {
    asm("popl  %edx\n\t"
	"popl  %ecx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st4, goto_st14, ST1, ST4, OPC_NONE) {
    asm("popl  %ecx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st4, goto_st24, ST2, ST4, OPC_NONE) {
    asm("xchg  %edx,%ecx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st4, goto_st34, ST3, ST4, OPC_NONE) {
    asm("movl  %ecx,%edx\n\t"
	"popl  %ecx");
    JUMP_TO_ACCUMULATOR; }
  CODE(opc_goto_st4, goto_st44, ST4, ST4, OPC_NONE) {
    JUMP_TO_ACCUMULATOR; }


  //
  // invocation related codes
  //

  // method head
#define LOCAL_VAR_AREA	12	// 4 * #local_var
#define SAVED_REG_AREA	8	// 8 bytes: %edi, %esi
  CODE_WITHOUT_DEBUG(opc_methodhead, head, STANY, STSTA, OPC_NONE) {
    asm("pushl %ebp\n\t"
	"movl  %esp,%ebp\n\t"
	"subl  $" STR(LOCAL_VAR_AREA) ",%esp\n\t"
	"pushl %edi\n\t"
	"pushl %esi");

#ifdef RUNTIME_DEBUG
    asm("pushl %eax");
    if (runtime_debug) {
      // class and method name
      DEBUG_IN;

      asm("movl %0,%%edi" : : "m"(mb));	// %edi = mb

      asm("movl  " METHOD_NAME(%edi) ",%eax\n\t"
	  "testl %eax,%eax\n\t"
	  "jz    methodhead_name_done\n\t"
	  "pushl %eax");
      asm("movl  " METHOD_CLAZZ(%edi) ",%eax");
      CB_NAME(%eax, %eax);	// break %edi
      asm("pushl %eax");
      PUSH_CONSTSTR("  %s#%s\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $12,%esp");
      FFLUSH;
      asm("methodhead_name_done:");

      // ebp and old var_base
      asm("pushl %esi\n\t"
	  "pushl %ebp");
      PUSH_CONSTSTR("  ebp: %x\n  old var base: %x\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $12,%esp");
      FFLUSH;

#  ifdef METAVM
      // write remote addr
      asm("movl  %0,%%edi" : : "m" (ee));
      asm("pushl  " EE_REMOTE_ADDR(%edi));
      asm("movsbl " EE_EXCEPTIONKIND(%edi) ",%edi\n\t"
	  "pushl  %edi");
      PUSH_CONSTSTR("  excKind: %d\n  remote addr: 0x%x\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $12,%esp");
      FFLUSH;
#  endif
      DEBUG_OUT;
    }
    asm("popl  %eax");
#endif	// RUNTIME_DEBUG

    // esi = var_base
    asm("movl  24(%ebp),%esi");
#ifdef RUNTIME_DEBUG
    asm("pushl %eax");
    if (runtime_debug) {
      DEBUG_IN;
      asm("pushl %esi");
      PUSH_CONSTSTR("  var base: 0x%08x\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $8,%esp");
      FFLUSH;
      DEBUG_OUT;
    }
    asm("popl  %eax");
#endif

#ifdef DIRECT_INVOCATION
    // ee->current_frame->vars = %ebp
    //   for CompiledFramePrev()
    asm("movl  %0,%%edi" : : "m" (ee));		// edi = ee
    asm("movl  " EE_CURRENTFRAME(%edi) ",%edi\n\t"
	"movl  %ebp," FRAME_VARS(%edi));
#endif	// DIRECT_INVOCATION

#ifdef SET_FRAME_MONITOR
    // ee->currentframe = 0
    asm("movl  %0,%%edi" : : "m" (ee));		// edi = ee
    asm("movl  " EE_CURRENTFRAME(%edi) ",%edi\n\t"
	"movl  $0," FRAME_MONITOR(%edi));
#endif
  }


  // actual start point of a method
  CODE(opc_start, start, STANY, STSTA, OPC_NONE) {}


  // actual end point of a method
  CODE(opc_end, end, STANY, STATE_AFTER_RETURN, OPC_NONE) {}


  // epilogue
  CODE(opc_epilogue, epilogue, STANY, STATE_AFTER_RETURN, OPC_NONE) {}


  // method tail
  CODE(opc_methodtail, tail, STANY, STSTA, OPC_NONE) {
    asm("leal  -" STR(LOCAL_VAR_AREA) "-" STR(SAVED_REG_AREA) "(%ebp),%esp");
		// -4 * (#local_var + #registers_on_stack)

#if 0	// disuse
    // eax = !exceptionOccurred(ee)
    asm("movl  %0,%%edi" : : "m" (ee));		// edi = ee
    asm("movsbl " EE_EXCEPTIONKIND(%edi) ",%eax");
#endif

    asm("popl  %esi\n\t"
	"popl  %edi");
    asm("leave\n\t"
	"ret");
  }


  // exception handler
	// now, eax,edx may be ename,DetailMessage
  CODE(opc_exc_handler, exc_handler, STANY, STSTA, OPC_NONE) {
    asm("call  " FUNCTION(exceptionHandler));
  }


  // enter synchronized method
#ifdef METAVM
#  define METAVM_MONITOR(OPTOP1_REG, METAVM_FUNCNAME, LABEL, STATE) \
    JUMP_IF_NOT_PROXY(OPTOP1_REG, LABEL "_local");\
    JUMP_IF_NOT_REMOTE(LABEL "_local");\
    \
    FUNCCALL_IN(STATE);\
    \
    asm("pushl " #OPTOP1_REG);\
    \
    asm("movl  %0,%%edi\n\t"		/* edi = ee */\
	"pushl %%edi" : : "m" (ee));	/* push edi */\
    \
    asm("call  " FUNCTION(METAVM_FUNCNAME) "\n\t"\
	"popl  %edi\n\t"	/* edi = ee */\
	"addl  $4,%esp");\
    \
    FUNCCALL_OUT(STATE);\
    \
    JUMP_IF_EXC_HASNT_OCCURRED(%edi /* is ee */, LABEL "_done");\
DEBUG_IN;\
PUSH_CONSTSTR("METAVM_MONITOR exc. occurred.\n");\
asm("call  " FUNCTION(printf) "\n\t"\
    "addl  $4,%esp");\
FFLUSH;\
DEBUG_OUT;\
    SIGNAL_ERROR_JUMP();\
    asm(LABEL "_local:")
#else
#  define METAVM_MONITOR(OPTOP1_REG, METAVM_FUNCNAME, LABEL, STATE)
#endif	// METAVM

#if JDK_VER >= 12
#  define CALL_MONITOR(MON, FUNCNAME) \
    asm("pushl " #MON);\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    asm("call  " FUNCTION(FUNCNAME) "\n\t"\
	"addl  $8,%esp")
#  define CALL_MONITORENTER(MON)	CALL_MONITOR(MON, monitorEnter2)
#  define CALL_MONITOREXIT(MON)		CALL_MONITOR(MON, monitorExit2)
#else
#  define CALL_MONITOR(MON, FUNCNAME) \
    asm("pushl " #MON "\n\t"\
	"call  " FUNCTION(monitorEnter) "\n\t"\
	"addl  $4,%esp")
#  define CALL_MONITORENTER(MON)	CALL_MONITOR(MON, monitorEnter)
#  define CALL_MONITOREXIT(MON)		CALL_MONITOR(MON, monitorExit)
#endif	// JDK_VER

  CODE(opc_sync_obj_enter, sync_obj_enter, STANY, STSTA, OPC_THROW) {
    // monitorEnter(obj_monitor(o));
    asm("movl  (%esi),%eax");	// eax = the 1st argument
#ifdef RUNTIME_DEBUG
    asm("pushl %eax");
    if (runtime_debug) {
      DEBUG_IN;
      asm("pushl %esi\n\tpushl %eax");
      PUSH_CONSTSTR("  obj: 0x%x @ 0x%x\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $12,%esp");
      FFLUSH;
      DEBUG_OUT;
    }
    asm("popl  %eax");
#endif
#ifdef SET_FRAME_MONITOR
    // frame->monitor = (struct sys_mon *)o
    asm("movl  %0,%%edi" : : "m" (ee));		// edi = ee
    asm("movl  " EE_CURRENTFRAME(%edi) ",%edi");
    asm("movl  %eax," FRAME_MONITOR(%edi));
#endif
    METAVM_MONITOR(%eax, proxy_monitorenter, "sync_obj_enter", 0);
    OBJ_MONITOR(%eax);
    CALL_MONITORENTER(%eax);
    asm("sync_obj_enter_done:");
  }


	// const: clazz
  CODE(opc_sync_static_enter, sync_static_enter, STANY, STSTA, OPC_NONE) {
    // monitorEnter(obj_monitor(clazz));
    asm("movl  $" STR(SLOT_CONST) ",%eax");	// eax = clazz
#ifdef SET_FRAME_MONITOR
    // frame->monitor = (struct sys_mon *)o
    asm("movl  %0,%%edi" : : "m" (ee));		// edi = ee
    asm("movl  " EE_CURRENTFRAME(%edi) ",%edi");
    asm("movl  %eax," FRAME_MONITOR(%edi));
#endif
    OBJ_MONITOR(%eax);
    CALL_MONITORENTER(%eax);
  }


  // exit synchronized method
  CODE(opc_sync_obj_exit, sync_obj_exit, STANY, STSTA, OPC_THROW) {
    FUNCCALL_IN(2);

    // monitorExit(obj_monitor(o))
    asm("movl  (%esi),%eax");	// eax = the 1st argument
#ifdef RUNTIME_DEBUG
    asm("pushl %eax");
    if (runtime_debug) {
      DEBUG_IN;
      asm("pushl %esi\n\tpushl %eax");
      PUSH_CONSTSTR("  obj: 0x%x @ 0x%x\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $12,%esp");
      FFLUSH;
      DEBUG_OUT;
    }
    asm("popl %eax");
#endif
    METAVM_MONITOR(%eax, proxy_monitorexit, "sync_obj_exit", 0);
    OBJ_MONITOR(%eax);
    CALL_MONITOREXIT(%eax);
    asm("sync_obj_exit_done:");

    FUNCCALL_OUT(2);
  }


	// const: clazz
  CODE(opc_sync_static_exit, sync_static_exit, STANY, STSTA, OPC_NONE) {
    FUNCCALL_IN(2);

    // monitorExit(obj_monitor(o))
    asm("movl  $" STR(SLOT_CONST) ",%eax");	// eax = clazz
    OBJ_MONITOR(%eax);
    CALL_MONITOREXIT(%eax);

    FUNCCALL_OUT(2);
  }


#ifdef METAVM
  CODE(opc_metavm_init, metavm_init, STANY, STSTA, OPC_NONE) {
    asm("movl  %0,%%edi" : : "m" (ee));
#  ifdef RUNTIME_DEBUG
    DEBUG_IN;
    asm("pushl %edi");
    PUSH_CONSTSTR("  ee: 0x%x\n");
    asm("call  " FUNCTION(printf) "\n\t"
	"addl  $8,%esp");
    FFLUSH;
    DEBUG_OUT;
#  endif
    asm("movl  $0," EE_REMOTE_ADDR(%edi) "\n\t"
	"movb  $1," EE_REMOTE_FLAG(%edi));
  }
#endif	// METAVM


#ifndef USE_SSE2
  CODE(opc_strict_enter, strict_enter, STANY, STSTA, OPC_NONE) {
#  ifdef STRICT_PRELOAD
    // push scales into FPU register
#    ifdef STRICT_USE_FSCALE
#      ifdef STRICT_FSCALE_USE_FLOAT
    asm("flds  %0\n\t" : : "m" (single_scale_neg));
    asm("flds  %0\n\t" : : "m" (double_scale_neg));
#      else
    asm("fildl %0\n\t" : : "m" (single_scale_neg));
    asm("fildl %0\n\t" : : "m" (double_scale_neg));
#      endif	// STRICT_FSCALE_USE_FLOAT
#    else		// STRICT_USE_FSCALE
    asm("fldt  %0\n\t" : : "m" (*single_scale_neg));
    asm("fldt  %0\n\t" : : "m" (*single_scale_pos));
    asm("fldt  %0\n\t" : : "m" (*double_scale_neg));
    asm("fldt  %0\n\t" : : "m" (*double_scale_pos));
#    endif	// STRICT_USE_FSCALE
#  endif	// STRICT_PRELOAD
  }

  CODE(opc_strict_exit, strict_exit, STANY, STSTA, OPC_NONE) {
#  ifdef STRICT_PRELOAD
    // pop scales from FPU register
#    ifdef STRICT_USE_FSCALE
    asm("fcompp");	// is equal to ffreep %st(0) x 2
#    else
    asm("fcompp\n\tfcompp");
#    endif	// STRICT_USE_FSCALE
#  endif	// STRICT_PRELOAD
  }

  // save FPU rounding precision
  CODE(opc_fppc_save, fppc_save, STANY, STSTA, OPC_NONE) {
    // save FPU control word
    asm("fstcw -6(%ebp)");	// preserved_fpucw
  }

  // restore FPU rounding precision
  CODE(opc_fppc_restore, fppc_restore, STANY, STSTA, OPC_NONE) {
    // restore FPU control word
    asm("fldcw -6(%ebp)");	// preserved_fpucw
  }

  // set FPU rounding precision to single, double, extended
#    define CODE_FPPC(PC, CHANGE_CW) \
  CODE(opc_fppc_##PC, fppc_##PC, STANY, STSTA, OPC_NONE) {\
    asm("movw  -6(%ebp),%ax");		/* preserved_fpucw */\
    asm(CHANGE_CW);\
    asm("movw  %ax,-8(%ebp)\n\t"	/* fpucw */\
	"fldcw -8(%ebp)");		/* fpucw */\
  }

  CODE_FPPC(single, "andl  $0xfcff,%eax");
  CODE_FPPC(double, "andl  $0xfcff,%eax\n\torl  $0x0200,%eax");
  CODE_FPPC(extended, "orl  $0x0300,%eax");
#endif	// !USE_SSE2


  // throw IllegalAccessError
  CODE(opc_throw_illegalaccess, throw_illegalaccess, STANY, ST0, OPC_THROW) {
    SIGNAL_ERROR1(EXCID_IllegalAccessError, "final or private field");
  }

  // throw InstantiationError
  CODE(opc_throw_instantiation, throw_instantiation, STANY, ST0, OPC_THROW) {
    SIGNAL_ERROR0(EXCID_InstantiationError);
  }

  // throw NoClassDefFoundError
  CODE(opc_throw_noclassdef, throw_noclassdef, STANY, ST0, OPC_THROW) {
    SIGNAL_ERROR0(EXCID_NoClassDefFoundError);
  }

  // throw NoSuchFieldError
  CODE(opc_throw_nofield, throw_nofield, STANY, ST0, OPC_THROW) {
    SIGNAL_ERROR0(EXCID_NoSuchFieldError);
  }

  // throw NoSuchMethodError
  CODE(opc_throw_nomethod, throw_nomethod, STANY, ST0, OPC_THROW) {
    SIGNAL_ERROR0(EXCID_NoSuchMethodError);
  }


#ifdef EXC_CHECK_IN_LOOP
  // check whether an exception has been thrown
  CODE(opc_exc_check, exc_check, STANY, STSTA, OPC_NONE) {
    // eax = !exceptionOccurred(ee)
    asm("movl  %0,%%edi" : : "m" (ee));		// edi = ee
    EE_EXCEPTIONKIND_EAX(%edi);
    asm("testl %eax,%eax\n\t"
	".short 0x850f\n\t.long " STR(SLOT_ADDR_EXC));	// jnz
  }
#endif	// EXC_CHECK_IN_LOOP


  // nop
  CODE(opc_nop, nop, STANY, STSTA, OPC_NONE) {}


  // iconst_0
  CODE(opc_iconst_0, [ifa]const_0, ST0, ST1, OPC_NONE) {
    asm("xorl  %edx,%edx");
  }
  CODE(opc_iconst_0, [ifa]const_0, ST1, ST2, OPC_NONE) {
    asm("xorl  %ecx,%ecx");
  }
  CODE(opc_iconst_0, [ifa]const_0, ST2, ST4, OPC_NONE) {
    asm("pushl %edx\n\t"
	"xorl  %edx,%edx");
  }
  CODE(opc_iconst_0, [ifa]const_0, ST3, ST4, OPC_NONE) {
    asm("xorl  %edx,%edx");
  }
  CODE(opc_iconst_0, [ifa]const_0, ST4, ST2, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"xorl  %ecx,%ecx");
  }


  // iconst_m1, iconst_[1-5]
#define CODE_ICONST_N(S, N) \
  CODE(opc_iconst_##S, iconst_##S, ST0, ST1, OPC_NONE) {\
    asm("movl  $" #N ",%edx"); }\
  CODE(opc_iconst_##S, iconst_##S, ST1, ST2, OPC_NONE) {\
    asm("movl  $" #N ",%ecx"); }\
  CODE(opc_iconst_##S, iconst_##S, ST2, ST4, OPC_NONE){\
    asm("pushl %edx\n\t"\
	"movl  $" #N ",%edx"); }\
  CODE(opc_iconst_##S, iconst_##S, ST3, ST4, OPC_NONE) {\
    asm("movl  $" #N ",%edx"); }\
  CODE(opc_iconst_##S, iconst_##S, ST4, ST2, OPC_NONE) {\
    asm("pushl %ecx\n\t"\
	"movl  $" #N ",%ecx"); }

#if 1
  CODE_ICONST_N(m1, -1);
  CODE_ICONST_N(1, 1);
#else
#  define CODE_ICONST_1(S, INSN) \
  CODE(opc_iconst_##S, iconst_##S, ST0, ST1, OPC_NONE) {\
    asm("xorl  %edx,%edx\n\t"\
	#INSN "  %edx"); }\
  CODE(opc_iconst_##S, iconst_##S, ST1, ST2, OPC_NONE) {\
    asm("xorl  %ecx,%ecx\n\t"\
	#INSN "  %ecx"); }\
  CODE(opc_iconst_##S, iconst_##S, ST2, ST4, OPC_NONE){\
    asm("pushl %edx");\
    asm("xorl  %edx,%edx\n\t"\
	#INSN "  %edx"); }\
  CODE(opc_iconst_##S, iconst_##S, ST3, ST4, OPC_NONE) {\
    asm("xorl  %edx,%edx\n\t"\
	#INSN "  %edx"); }\
  CODE(opc_iconst_##S, iconst_##S, ST4, ST2, OPC_NONE) {\
    asm("pushl %ecx");\
    asm("xorl  %ecx,%ecx\n\t"\
	#INSN "  %ecx"); }

  CODE_ICONST_1(1, incl);
  CODE_ICONST_1(m1, decl);
#endif

  CODE_ICONST_N(2, 2);
  CODE_ICONST_N(3, 3);
  CODE_ICONST_N(4, 4);
  CODE_ICONST_N(5, 5);


  // lconst_0
  CODE(opc_lconst_0, [ld]const_0, ST0, ST2, OPC_NONE) {
    asm("xorl  %edx,%edx\n\t"
	"xorl  %ecx,%ecx");
  }
  CODE(opc_lconst_0, [ld]const_0, ST1, ST2, OPC_NONE) {
    asm("pushl %edx\n\t"
	"xorl  %ecx,%ecx\n\t"
	"xorl  %edx,%edx");
  }
  CODE(opc_lconst_0, [ld]const_0, ST2, ST2, OPC_NONE) {
    asm("pushl %edx\n\t"
	"pushl %ecx\n\t"
	"xorl  %edx,%edx\n\t"
	"xorl  %ecx,%ecx");
  }
  CODE(opc_lconst_0, [ld]const_0, ST3, ST2, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"xorl  %edx,%edx\n\t"
	"xorl  %ecx,%ecx");
  }
  CODE(opc_lconst_0, [ld]const_0, ST4, ST2, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"pushl %edx\n\t"
	"xorl  %ecx,%ecx\n\t"
	"xorl  %edx,%edx");
  }


  // lconst_1
#define LCONST_ST02 \
    asm("movl  $1,%ecx\n\t"\
	"xorl  %edx,%edx")
	// now state 2

#define LCONST_ST04 \
    asm("movl  $1,%edx\n\t"\
	"xorl  %ecx,%ecx")
	// now state 4

  CODE(opc_lconst_1, lconst_1, ST0, ST2, OPC_NONE) {
    LCONST_ST02;
  }
  CODE(opc_lconst_1, lconst_1, ST1, ST2, OPC_NONE) {
    asm("pushl %edx");	// now state 0
    LCONST_ST02;
  }
  CODE(opc_lconst_1, lconst_1, ST2, ST4, OPC_NONE) {
    asm("pushl %edx\n\t"
	"pushl %ecx");	// now state 0
    LCONST_ST04;
  }
  CODE(opc_lconst_1, lconst_1, ST3, ST4, OPC_NONE) {
    asm("pushl %ecx");	// now state 0
    LCONST_ST04;
  }
  CODE(opc_lconst_1, lconst_1, ST4, ST2, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"pushl %edx");	// now state 0
    LCONST_ST02;
  }


  // fconst_1
  CODE(opc_fconst_1, fconst_1, ST0, ST1, OPC_NONE) {
    asm("movl  $0x3f800000,%edx");
  }
  CODE(opc_fconst_1, fconst_1, ST1, ST2, OPC_NONE) {
    asm("movl  $0x3f800000,%ecx");
  }
  CODE(opc_fconst_1, fconst_1, ST2, ST4, OPC_NONE) {
    asm("pushl %edx\n\t"
	"movl  $0x3f800000,%edx");
  }
  CODE(opc_fconst_1, fconst_1, ST3, ST4, OPC_NONE) {
    asm("movl  $0x3f800000,%edx");
  }
  CODE(opc_fconst_1, fconst_1, ST4, ST2, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"movl  $0x3f800000,%ecx");
  }


  // fconst_2
  CODE(opc_fconst_2, fconst_2, ST0, ST1, OPC_NONE) {
    asm("movl  $0x40000000,%edx");
  }
  CODE(opc_fconst_2, fconst_2, ST1, ST2, OPC_NONE) {
    asm("movl  $0x40000000,%ecx");
  }
  CODE(opc_fconst_2, fconst_2, ST2, ST4, OPC_NONE) {
    asm("pushl %edx\n\t"
	"movl  $0x40000000,%edx");
  }
  CODE(opc_fconst_2, fconst_2, ST3, ST4, OPC_NONE) {
    asm("movl  $0x40000000,%edx");
  }
  CODE(opc_fconst_2, fconst_2, ST4, ST2, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"movl  $0x40000000,%ecx");
  }


  // dconst_1
#define DCONST_ST02 \
    asm("movl  $0x3ff00000,%edx\n\t"	/* high word */\
	"xorl  %ecx,%ecx")		/* low word */

#define DCONST_ST04 \
    asm("movl  $0x3ff00000,%ecx\n\t"	/* high word */\
	"xorl  %edx,%edx")		/* low word */

  CODE(opc_dconst_1, dconst_1, ST0, ST2, OPC_NONE) {
    DCONST_ST02;
  }
  CODE(opc_dconst_1, dconst_1, ST1, ST4, OPC_NONE) {
    asm("pushl %edx");	// now state 0
    DCONST_ST04;
  }
  CODE(opc_dconst_1, dconst_1, ST2, ST2, OPC_NONE) {
    asm("pushl %edx\n\t"
	"pushl %ecx");	// now state 0
    DCONST_ST02;
  }
  CODE(opc_dconst_1, dconst_1, ST3, ST2, OPC_NONE) {
    asm("pushl %ecx");	// now state 0
    DCONST_ST02;
  }
  CODE(opc_dconst_1, dconst_1, ST4, ST4, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"pushl %edx");	// now state 0
    DCONST_ST04;
  }


  // bipush
	// const: value
  CODE(opc_bipush, a_const, ST0, ST1, OPC_NONE) {
    asm("movl  $" STR(SLOT_CONST) ",%edx");
    VALUE_DEBUG(%edx);
  }
  CODE(opc_bipush, a_const, ST1, ST2, OPC_NONE) {
    asm("movl  $" STR(SLOT_CONST) ",%ecx");
    VALUE_DEBUG(%ecx);
  }
  CODE(opc_bipush, a_const, ST2, ST4, OPC_NONE) {
    asm("pushl %edx\n\t"
	"movl  $" STR(SLOT_CONST) ",%edx");
    VALUE_DEBUG(%edx);
  }
  CODE(opc_bipush, a_const, ST3, ST4, OPC_NONE) {
    asm("movl  $" STR(SLOT_CONST) ",%edx");
    VALUE_DEBUG(%edx);
  }
  CODE(opc_bipush, a_const, ST4, ST2, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"movl  $" STR(SLOT_CONST) ",%ecx");
    VALUE_DEBUG(%ecx);
  }


  // ldc2_w
	// const: val[32:63], val[0:31]
  CODE(opc_ldc2_w, ldc2_w, ST0, ST2, OPC_NONE) {
    asm("movl  $" STR(SLOT_CONST) ",%edx\n\t"
	"movl  $" STR(SLOT_CONST) ",%ecx");
    VALUE64_DEBUG(%ecx, %edx);
  }
  CODE(opc_ldc2_w, ldc2_w, ST1, ST4, OPC_NONE) {
    asm("pushl %edx\n\t"
	"movl  $" STR(SLOT_CONST) ",%ecx\n\t"
	"movl  $" STR(SLOT_CONST) ",%edx");
    VALUE64_DEBUG(%edx, %ecx);
  }
  CODE(opc_ldc2_w, ldc2_w, ST2, ST2, OPC_NONE) {
    asm("pushl %edx\n\t"
	"pushl %ecx\n\t"
	"movl  $" STR(SLOT_CONST) ",%edx\n\t"
	"movl  $" STR(SLOT_CONST) ",%ecx");
    VALUE64_DEBUG(%ecx, %edx);
  }
  CODE(opc_ldc2_w, ldc2_w, ST3, ST2, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"movl  $" STR(SLOT_CONST) ",%edx\n\t"
	"movl  $" STR(SLOT_CONST) ",%ecx");
    VALUE64_DEBUG(%ecx, %edx);
  }
  CODE(opc_ldc2_w, ldc2_w, ST4, ST4, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"pushl %edx\n\t"
	"movl  $" STR(SLOT_CONST) ",%ecx\n\t"
	"movl  $" STR(SLOT_CONST) ",%edx");
    VALUE64_DEBUG(%edx, %ecx);
  }


  // iload
	// const: index * 4
#ifdef RUNTIME_DEBUG
#  define ILOAD_DEBUG1(VAL) \
  asm("pushl %eax");\
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl " #VAL "\n\t"\
	"flds  (%esp)\n\t"\
	"subl  $4,%esp\n\t"\
	"fstpl (%esp)\n\t"\
	"pushl " #VAL "\n\t"\
	"pushl " #VAL "\n\t"\
	"pushl $" STR(SLOT_CONST));\
    PUSH_CONSTSTR("  [%d] 0x%08x %d %g\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $24,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#else
#  define ILOAD_DEBUG1(VAL)
#endif
  CODE(opc_iload, [ifa]load, ST0, ST1, OPC_NONE) {
    asm("movl " STR(SLOT_CONST) "(%esi),%edx");
    ILOAD_DEBUG1(%edx);
  }
  CODE(opc_iload, [ifa]load, ST1, ST2, OPC_NONE) {
    asm("movl " STR(SLOT_CONST) "(%esi),%ecx");
    ILOAD_DEBUG1(%ecx);
  }
  CODE(opc_iload, [ifa]load, ST2, ST4, OPC_NONE) {
    asm("pushl %edx\n\t"
	"movl  " STR(SLOT_CONST) "(%esi),%edx");
    ILOAD_DEBUG1(%edx);
  }
  CODE(opc_iload, [ifa]load, ST3, ST4, OPC_NONE) {
    asm("movl " STR(SLOT_CONST) "(%esi),%edx");
    ILOAD_DEBUG1(%edx);
  }
  CODE(opc_iload, [ifa]load, ST4, ST2, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"movl  " STR(SLOT_CONST) "(%esi),%ecx");
    ILOAD_DEBUG1(%ecx);
  }

#ifdef OPTIMIZE_INTERNAL_CODE
  CODE(opc_fload_fld, fload_fld, ST0, STSTA, OPC_NONE) {
    asm("subl  $4,%esp\n\t");	// to simulate the true value of %esp
#  ifdef USE_SSE2
    asm("movss " STR(SLOT_CONST) "(%esi),%xmm0");
#  else
    asm("flds " STR(SLOT_CONST) "(%esi)");
#  endif

#ifdef RUNTIME_DEBUG
    if (runtime_debug) {
      DEBUG_IN;
      asm("subl  $8,%esp\n\t"
	  "fstl  (%esp)");
      PUSH_CONSTSTR("  %g\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $12,%esp");
      FFLUSH;
      DEBUG_OUT;
    }
#endif
  }
#endif	// OPTIMIZE_INTERNAL_CODE


  // lload
	// const: index * 4, (index + 1) * 4
#ifdef RUNTIME_DEBUG
#  define LLOAD_DEBUG1(OPTOP1_REG, OPTOP2_REG) \
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl " #OPTOP2_REG "\n\tpushl " #OPTOP1_REG "\n\t"\
	"pushl " #OPTOP2_REG "\n\tpushl " #OPTOP1_REG "\n\t"\
	"pushl " #OPTOP2_REG "\n\tpushl " #OPTOP1_REG "\n\t"\
	"movl  $" STR(SLOT_CONST) ",%eax\n\t"\
	"pushl %eax");\
    PUSH_CONSTSTR("  var[%d]: 0x%016llx, %lld, %g\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $32,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }
#else
#  define LLOAD_DEBUG1(OPTOP1_REG, OPTOP2_REG)
#endif

#define LLOAD_ST02 \
    asm("movl  " STR(SLOT_CONST) "(%esi),%ecx\n\t"\
	"movl  " STR(SLOT_CONST) "(%esi),%edx");\
    LLOAD_DEBUG1(%ecx, %edx)
#define LLOAD_ST04 \
    asm("movl  " STR(SLOT_CONST) "(%esi),%edx\n\t"\
	"movl  " STR(SLOT_CONST) "(%esi),%ecx");\
    LLOAD_DEBUG1(%edx, %ecx)

  CODE(opc_lload, [ld]load, ST0, ST2, OPC_NONE) {
    LLOAD_ST02;
  }
  CODE(opc_lload, [ld]load, ST1, ST2, OPC_NONE) {
    asm("pushl %edx");	// now state 0
    LLOAD_ST02;
  }
  CODE(opc_lload, [ld]load, ST2, ST4, OPC_NONE) {
    asm("pushl %edx\n\t"
	"pushl %ecx");	// now state 0
    LLOAD_ST04;
  }
  CODE(opc_lload, [ld]load, ST3, ST4, OPC_NONE) {
    asm("pushl %ecx");	// now state 0
    LLOAD_ST04;
  }
  CODE(opc_lload, [ld]load, ST4, ST2, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"pushl %edx");	// now state 0
    LLOAD_ST02;
  }

#ifdef OPTIMIZE_INTERNAL_CODE
  CODE(opc_dload_dld, dload_dld, ST0, STSTA, OPC_NONE) {
    asm("subl  $8,%esp\n\t");	// to simulate the true value of %esp
#  ifdef USE_SSE2
    asm("movsd " STR(SLOT_CONST) "(%esi),%xmm0");
#  else
    asm("fldl  " STR(SLOT_CONST) "(%esi)");
#  endif

#ifdef RUNTIME_DEBUG
    if (runtime_debug) {
      DEBUG_IN;
      asm("subl  $8,%esp\n\t"
	  "fstl  (%esp)");
      PUSH_CONSTSTR("  %g\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $12,%esp");
      FFLUSH;
      DEBUG_OUT;
    }
#endif
  }
#endif	// OPTIMIZE_INTERNAL_CODE


  //
  // array access
  //

  // array_check
#if 0
  CODE(opc_array_check, array_check, ST[24], ST[24], OPC_SIGNAL) {
    if (!h) { SIGNAL_ERROR0(EXCID_NullPointerException); }

    if ((index < 0) || (index >= obj_length(h))) {
      SIGNAL_ERROR0(EXCID_ArrayIndexOutOfBoundsException);
    }
  }
#endif

#ifdef RUNTIME_DEBUG
#  define ARRAY_CHECK_DEBUG1(INDEX) \
  asm("pushl %eax");\
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl " #INDEX);\
    PUSH_CONSTSTR("  index:  %d\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $8,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#  define ARRAY_CHECK_DEBUG2(LEN) \
  asm("pushl %eax");\
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl " #LEN);\
    PUSH_CONSTSTR("  length: %d\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $8,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#else
#  define ARRAY_CHECK_DEBUG1(INDEX)
#  define ARRAY_CHECK_DEBUG2(INDEX)
#endif

#if defined(METAVM) && !defined(METAVM_NO_ARRAY)
#  define METAVM_ARRAY_CHECK(HANDLE, LABEL) \
	/* skip local check */\
    asm("pushl %edi\n\tpushl %eax");	/* save handle and index */\
    \
    asm("movl  " #HANDLE ",%eax");	/* eax = handle */\
    JUMP_IF_NOT_PROXY(%eax, LABEL "_arychk_local");\
    JUMP_IF_NOT_REMOTE(LABEL "_arychk_local");\
    \
    asm("popl  %eax\n\tpopl  %edi\n\t"	/* restore */\
	"jmp  " LABEL "_arychk_done\n\t"\
      LABEL "_arychk_local:\n\t"\
	"popl  %eax\n\tpopl  %edi");	/* restore */
#else
#  define METAVM_ARRAY_CHECK(HANDLE, LABEL)
#endif	// METAVM_NO_ARRAY

#ifndef NO_NULL_AND_ARRAY_CHECK
#  define ARRAY_CHECK(HANDLE, INDEX, LABEL) \
    ARRAY_CHECK_DEBUG1(INDEX);\
    \
    NULL_TEST(HANDLE, LABEL "_1");\
    \
    METAVM_ARRAY_CHECK(HANDLE, LABEL);\
    \
    /* edi = length */\
    OBJ_LENGTH(HANDLE, %edi);			/* edi = obj_length(handle) */\
    ARRAY_CHECK_DEBUG2(%edi);\
    \
    /* check if (index < 0 || index >= length) */\
    asm("cmpl  %edi," #INDEX "\n\t"\
	"jc    " LABEL "_arychk_done");\
    SIGNAL_ERROR0(EXCID_ArrayIndexOutOfBoundsException);\
    asm(LABEL "_arychk_done:")		/* label */
#else
#  define ARRAY_CHECK(HANDLE, INDEX, LABEL) \
    METAVM_ARRAY_CHECK(HANDLE, LABEL);\
    OBJ_LENGTH(HANDLE, %edi);			/* edi = obj_length(handle) */\
    asm(LABEL "_arychk_done:")
#endif	// NO_NULL_AND_ARRAY_CHECK
	// destroy %edi
	// edi = obj_length(handle)

  CODE(opc_array_check, array_check, ST2, ST2, OPC_SIGNAL) {
    ARRAY_CHECK(%edx, %ecx, "arychk_st2");
  }
  CODE(opc_array_check, array_check, ST4, ST4, OPC_SIGNAL) {
    ARRAY_CHECK(%ecx, %edx, "arychk_st4");
  }


  // iaload
	// compile: fill_cache, array_check, iaload
#ifdef METAVM
#  define METAVM_READ(HANDLE, SLOT, TGT, LABEL, STATE, FUNC32, FUNCOBJ) \
    JUMP_IF_NOT_PROXY(HANDLE, LABEL "_local");\
    JUMP_IF_NOT_REMOTE(LABEL "_local");\
    \
    FUNCCALL_IN(STATE);\
    \
    asm("pushl " #SLOT "\n\t"	/* slot */\
	"pushl " #HANDLE);	/* obj (Proxy) */\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    \
    asm("movl  $" STR(SLOT_CONST) ",%edi");\
    asm("testl %edi,%edi\n\t"\
	"jnz   " LABEL "_obj\n\t"\
	"call  " FUNCTION(FUNC32) "\n\t"\
	"jmp   " LABEL "_remoteget_done\n\t"\
      LABEL "_obj:\n\t"\
	"call  " FUNCTION(FUNCOBJ) "\n\t"\
      LABEL "_remoteget_done:");\
    asm("popl  %edi\n\t"	/* edi = ee */\
	"addl  $8,%esp");\
    \
    FUNCCALL_OUT(STATE);\
    \
    asm("movl  %eax," #TGT);\
    \
    JUMP_IF_EXC_HASNT_OCCURRED(%edi /* is ee */, LABEL "_done");\
DEBUG_IN;\
PUSH_CONSTSTR("METAVM_READ exc. occurred.\n");\
asm("call  " FUNCTION(printf) "\n\t"\
    "addl  $4,%esp");\
FFLUSH;\
DEBUG_OUT;\
    SIGNAL_ERROR_JUMP();\
    \
    asm(LABEL "_local:")
#  define METAVM_GETFIELD(HANDLE, SLOT, TGT, LABEL, STATE) \
   METAVM_READ(HANDLE, SLOT, TGT, LABEL, STATE, proxy_get32field, proxy_getobjfield)
#else
#  define METAVM_GETFIELD(HANDLE, SLOT, TGT, LABEL, STATE)
#endif	// METAVM

#if defined(METAVM) && !defined(METAVM_NO_ARRAY)
#  define METAVM_ALOAD(HANDLE, SLOT, TGT, LABEL, STATE) \
   METAVM_READ(HANDLE, SLOT, TGT, LABEL, STATE, proxy_aload32, proxy_aloadobj)
#else
#  define METAVM_ALOAD(HANDLE, SLOT, TGT, LABEL, STATE)
#endif	// METAVM_NO_ARRAY

  CODE(opc_iaload, [ifa]aload, ST2, ST1, OPC_THROW) {
    METAVM_ALOAD(%edx, %ecx, %edx, "iaload_st2", 0);
    UNHAND(%edx, %eax);
    asm("movl  (%eax,%ecx,4),%edx");
    asm("iaload_st2_done:");
    VALUE_DEBUG(%edx);
  }
  CODE(opc_iaload, [ifa]aload, ST4, ST3, OPC_THROW) {
    METAVM_ALOAD(%ecx, %edx, %ecx, "iaload_st4", 0);
    UNHAND(%ecx, %eax);
    asm("movl  (%eax,%edx,4),%ecx");
    asm("iaload_st4_done:");
    VALUE_DEBUG(%ecx);
  }


#ifdef OPTIMIZE_INTERNAL_CODE
  CODE(opc_faload_fld, faload_fld, ST2, ST1, OPC_THROW) {
    METAVM_ALOAD(%edx, %ecx, %edx, "faload_fld_st2", 0);
    UNHAND(%edx, %eax);
#  ifdef USE_SSE2
    asm("movss (%eax,%ecx,4),%xmm0");
#  else
    asm("flds  (%eax,%ecx,4)");
#  endif
    // omit asm("subl  $4,%esp");
    asm("faload_fld_st2_done:");
  }
  CODE(opc_faload_fld, faload_fld, ST4, ST1, OPC_THROW) {
    METAVM_ALOAD(%ecx, %edx, %ecx, "faload_fld_st4", 0);
    UNHAND(%ecx, %eax);
#  ifdef USE_SSE2
    asm("movss (%eax,%edx,4),%xmm0");
#  else
    asm("flds  (%eax,%edx,4)");
#  endif
    // omit asm("subl  $4,%esp");
    asm("faload_fld_st4_done:");
  }
#endif	// OPTIMIZE_INTERNAL_CODE


  // laload
	// compile: fill_cache, array_check, laload
#ifdef METAVM
#  define METAVM_READ2(HANDLE, SLOT, TGT_LOW, TGT_HIGH, LABEL, FUNC) \
    JUMP_IF_NOT_PROXY(HANDLE, LABEL "_local");\
    JUMP_IF_NOT_REMOTE(LABEL "_local");\
    \
    asm("pushl " #SLOT "\n\t"	/* slot */\
	"pushl " #HANDLE);	/* obj (Proxy) */\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    \
    asm("call  " FUNCTION(FUNC) "\n\t"\
	"movl  %edx," #TGT_HIGH "\n\t"\
	"popl  %edi\n\t"	/* edi = ee */\
	"movl  %eax," #TGT_LOW "\n\t"\
	"addl  $8,%esp");\
    \
    JUMP_IF_EXC_HASNT_OCCURRED(%edi /* is ee */, LABEL "_done");\
DEBUG_IN;\
PUSH_CONSTSTR("METAVM_READ2 exc. occurred.\n");\
asm("call  " FUNCTION(printf) "\n\t"\
    "addl  $4,%esp");\
FFLUSH;\
DEBUG_OUT;\
    SIGNAL_ERROR_JUMP();\
    \
    asm(LABEL "_local:")
#  define METAVM_GETFIELD2(HANDLE, SLOT, TGT_LOW, TGT_HIGH, LABEL) \
   METAVM_READ2(HANDLE, SLOT, TGT_LOW, TGT_HIGH, LABEL, proxy_get64field)
#else
#  define METAVM_GETFIELD2(HANDLE, SLOT, TGT_LOW, TGT_HIGH, LABEL)
#endif	// METAVM

#if defined(METAVM) && !defined(METAVM_NO_ARRAY)
#  define METAVM_ALOAD2(HANDLE, SLOT, TGT_LOW, TGT_HIGH, LABEL) \
   METAVM_READ2(HANDLE, SLOT, TGT_LOW, TGT_HIGH, LABEL, proxy_aload64)
#else
#  define METAVM_ALOAD2(HANDLE, SLOT, TGT_LOW, TGT_HIGH, LABEL)
#endif	// METAVM_NO_ARRAY

  CODE(opc_laload, [ld]aload, ST2, ST4, OPC_THROW) {
    METAVM_ALOAD2(%edx, %ecx, %edx, %ecx, "laload_st2");
    UNHAND(%edx, %eax);
    asm("leal  (%eax,%ecx,8),%edi\n\t"
	"movl  (%edi),%edx\n\t"
	"movl  4(%edi),%ecx");
    asm("laload_st2_done:");
    VALUE64_DEBUG(%edx, %ecx);
  }
  CODE(opc_laload, [ld]aload, ST4, ST2, OPC_THROW) {
    METAVM_ALOAD2(%ecx, %edx, %ecx, %edx, "laload_st4");
    UNHAND(%ecx, %eax);
    asm("leal  (%eax,%edx,8),%edi\n\t"
	"movl  (%edi),%ecx\n\t"
	"movl  4(%edi),%edx");
    asm("laload_st4_done:");
    VALUE64_DEBUG(%ecx, %edx);
  }


#ifdef OPTIMIZE_INTERNAL_CODE
  CODE(opc_daload_dld, [ld]aload, ST2, ST2, OPC_THROW) {
    METAVM_ALOAD2(%edx, %ecx, %edx, %ecx, "daload_dld_st2");
    UNHAND(%edx, %eax);
#  ifdef USE_SSE2
    asm("movsd (%eax,%ecx,8),%xmm0");
#  else
    asm("fldl  (%eax,%ecx,8)");
#  endif
    // omit asm("subl  $8,%esp");
    asm("daload_dld_st2_done:");
  }
  CODE(opc_daload_dld, [ld]aload, ST4, ST2, OPC_THROW) {
    METAVM_ALOAD2(%ecx, %edx, %ecx, %edx, "daload_dld_st4");
    UNHAND(%ecx, %eax);
#  ifdef USE_SSE2
    asm("movsd (%eax,%edx,8),%xmm0");
#  else
    asm("fldl  (%eax,%edx,8)");
#  endif
    // omit asm("subl  $8,%esp");
    asm("daload_dld_st4_done:");
  }
#endif	// OPTIMIZE_INTERNAL_CODE


  // baload
	// compile: fill_cache, array_check, baload
  CODE(opc_baload, baload, ST2, ST1, OPC_THROW) {
    METAVM_ALOAD(%edx, %ecx, %edx, "baload_st2", 0);
    UNHAND(%edx, %eax);
    asm("movsbl (%eax,%ecx),%edx");
    asm("baload_st2_done:");
    VALUE_DEBUG(%edx);
  }
  CODE(opc_baload, baload, ST4, ST3, OPC_THROW) {
    METAVM_ALOAD(%ecx, %edx, %ecx, "baload_st4", 0);
    UNHAND(%ecx, %eax);
    asm("movsbl (%eax,%edx),%ecx");
    asm("baload_st4_done:");
    VALUE_DEBUG(%ecx);
  }


  // caload
	// compile: fill_cache, array_check, caload
  CODE(opc_caload, caload, ST2, ST1, OPC_THROW) {
    METAVM_ALOAD(%edx, %ecx, %edx, "caload_st2", 0);
    UNHAND(%edx, %eax);
    asm("movzwl (%eax,%ecx,2),%edx");
    asm("caload_st2_done:");
    VALUE_DEBUG(%edx);
  }
  CODE(opc_caload, caload, ST4, ST3, OPC_THROW) {
    METAVM_ALOAD(%ecx, %edx, %ecx, "caload_st4", 0);
    UNHAND(%ecx, %eax);
    asm("movzwl (%eax,%edx,2),%ecx");
    asm("caload_st4_done:");
    VALUE_DEBUG(%ecx);
  }


  // saload
	// compile: fill_cache, array_check, saload
  CODE(opc_saload, saload, ST2, ST1, OPC_THROW) {
    METAVM_ALOAD(%edx, %ecx, %edx, "saload_st2", 0);
    UNHAND(%edx, %eax);
    asm("movswl (%eax,%ecx,2),%edx");
    asm("saload_st2_done:");
    VALUE_DEBUG(%edx);
  }
  CODE(opc_saload, saload, ST4, ST3, OPC_THROW) {
    METAVM_ALOAD(%ecx, %edx, %ecx, "saload_st4", 0);
    UNHAND(%ecx, %eax);
    asm("movswl (%eax,%edx,2),%ecx");
    asm("saload_st4_done:");
    VALUE_DEBUG(%ecx);
  }


  // istore
	// const: index * 4
#define CODE_ISTORE(VOP, ST_A, ST_B, ST_C, ST_D, ST_E) \
  CODE(opc_##VOP, VOP, ST0, ST_A, OPC_NONE) {\
    asm("popl  %edx\n\t"\
	"movl  %edx," STR(SLOT_CONST) "(%esi)");\
    ILOAD_DEBUG1(%edx);\
  }\
  CODE(opc_##VOP, VOP, ST1, ST_B, OPC_NONE) {\
    asm("movl  %edx," STR(SLOT_CONST) "(%esi)");\
    ILOAD_DEBUG1(%edx);\
  }\
  CODE(opc_##VOP, VOP, ST2, ST_C, OPC_NONE) {\
    asm("movl  %ecx," STR(SLOT_CONST) "(%esi)");\
    ILOAD_DEBUG1(%ecx);\
  }\
  CODE(opc_##VOP, VOP, ST3, ST_D, OPC_NONE) {\
    asm("movl  %ecx," STR(SLOT_CONST) "(%esi)");\
    ILOAD_DEBUG1(%ecx);\
  }\
  CODE(opc_##VOP, VOP, ST4, ST_E, OPC_NONE) {\
    asm("movl  %edx," STR(SLOT_CONST) "(%esi)");\
    ILOAD_DEBUG1(%edx);\
  }

  CODE_ISTORE(istore, ST0, ST0, ST1, ST0, ST3);
#ifdef OPTIMIZE_INTERNAL_CODE
  CODE_ISTORE(istld, ST1, ST1, ST2, ST3, ST4);
#endif

#ifdef OPTIMIZE_INTERNAL_CODE
  CODE(opc_fst_fstore, fst_fstore, ST0, ST0, OPC_NONE) {
    asm("addl  $4,%esp");
#  ifdef USE_SSE2
    asm("movss %xmm0," STR(SLOT_CONST) "(%esi)");
#  else
    asm("fstps " STR(SLOT_CONST) "(%esi)");
#  endif
  }
#endif


  // lstore
	// const: index * 4, (index + 1) * 4
	// compile: fill_cache, lstore
#define CODE_LSTORE(VOP, ST_A, ST_B) \
  CODE(opc_##VOP, VOP, ST2, ST_A, OPC_NONE) {\
    asm("movl  %ecx," STR(SLOT_CONST) "(%esi)\n\t"\
	"movl  %edx," STR(SLOT_CONST) "(%esi)");\
    LLOAD_DEBUG1(%ecx, %edx);\
  }\
  CODE(opc_##VOP, VOP, ST4, ST_B, OPC_NONE) {\
    asm("movl  %edx," STR(SLOT_CONST) "(%esi)\n\t"\
	"movl  %ecx," STR(SLOT_CONST) "(%esi)");\
    LLOAD_DEBUG1(%edx, %ecx);\
  }

  CODE_LSTORE(lstore, ST0, ST0);
#ifdef OPTIMIZE_INTERNAL_CODE
  CODE_LSTORE(lstld, ST2, ST4);
#endif

#ifdef OPTIMIZE_INTERNAL_CODE
  CODE(opc_dst_dstore, dst_dstore, ST0, ST0, OPC_NONE) {
    asm("addl  $8,%esp");	// substitution for fill_cache
#  ifdef USE_SSE2
    asm("movsd %xmm0," STR(SLOT_CONST) "(%esi)");
#  else
    asm("fstpl " STR(SLOT_CONST) "(%esi)");
#  endif
  }
#endif


  // iastore
	// compile: iastore1, fill_cache, array_check, iastore
  CODE(opc_iastore1, [ifa]astore1, ST0, ST0, OPC_NONE) {
    asm("popl  %eax");
    VALUE_DEBUG(%eax);
  }
  CODE(opc_iastore1, [ifa]astore1, ST1, ST0, OPC_NONE) {
    asm("movl  %edx,%eax");
    VALUE_DEBUG(%eax);
  }
  CODE(opc_iastore1, [ifa]astore1, ST2, ST1, OPC_NONE) {
    asm("movl  %ecx,%eax");
    VALUE_DEBUG(%eax);
  }
  CODE(opc_iastore1, [ifa]astore1, ST3, ST0, OPC_NONE) {
    asm("movl  %ecx,%eax");
    VALUE_DEBUG(%eax);
  }
  CODE(opc_iastore1, [ifa]astore1, ST4, ST3, OPC_NONE) {
    asm("movl  %edx,%eax");
    VALUE_DEBUG(%eax);
  }
	// eax: value

#ifdef METAVM
#  define METAVM_WRITE(HANDLE, SLOT, VAL, LABEL, STATE, FUNC32, FUNCOBJ) \
    JUMP_IF_NOT_PROXY(HANDLE, LABEL "_local");\
    JUMP_IF_NOT_REMOTE(LABEL "_local");\
    \
    FUNCCALL_IN(STATE);\
    \
    asm("pushl " #VAL "\n\t"/* value */\
	"pushl " #SLOT "\n\t"	/* slot */\
	"pushl " #HANDLE);	/* obj (Proxy) */\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    \
    asm("movl  $" STR(SLOT_CONST) ",%edi");\
    asm("testl %edi,%edi\n\t"\
	"jnz   " LABEL "_obj\n\t"\
	"call  " FUNCTION(FUNC32) "\n\t"\
	"jmp   " LABEL "_return\n\t"\
      LABEL "_obj:\n\t"\
	"call  " FUNCTION(FUNCOBJ));\
    asm(LABEL "_return:\n\t"\
	"popl  %edi\n\t"	/* edi = ee */\
	"addl  $12,%esp");\
    \
    FUNCCALL_OUT(STATE);\
    \
    JUMP_IF_EXC_HASNT_OCCURRED(%edi /* is ee */, LABEL "_done");\
DEBUG_IN;\
PUSH_CONSTSTR("METAVM_WRITE exc. occurred.\n");\
asm("call  " FUNCTION(printf) "\n\t"\
    "addl  $4,%esp");\
FFLUSH;\
DEBUG_OUT;\
    SIGNAL_ERROR_JUMP();\
    \
    asm(LABEL "_local:")
#  define METAVM_PUTFIELD(HANDLE, SLOT, VAL, LABEL, STATE) \
   METAVM_WRITE(HANDLE, SLOT, VAL, LABEL, STATE, proxy_put32field, proxy_putobjfield)
#else
#  define METAVM_PUTFIELD(HANDLE, SLOT, VAL, LABEL, STATE)
#endif	// METAVM

#if defined(METAVM) && !defined(METAVM_NO_ARRAY)
#  define METAVM_ASTORE(HANDLE, SLOT, VAL, LABEL, STATE) \
   METAVM_WRITE(HANDLE, SLOT, VAL, LABEL, STATE, proxy_astore32,proxy_astoreobj)
#else
#  define METAVM_ASTORE(HANDLE, SLOT, VAL, LABEL, STATE)
#endif	// METAVM_NO_ARRAY

  CODE(opc_iastore, [ifa]astore, ST2, ST0, OPC_THROW) {
    METAVM_ASTORE(%edx, %ecx, %eax, "iastore_st2", 2);
    UNHAND(%edx, %edi);
    asm("movl  %eax,(%edi,%ecx,4)");	// array->body[ecx] = eax
    asm("iastore_st2_done:");
  }
  CODE(opc_iastore, [ifa]astore, ST4, ST0, OPC_THROW) {
    METAVM_ASTORE(%ecx, %edx, %eax, "iastore_st4", 4);
    UNHAND(%ecx, %edi);
    asm("movl  %eax,(%edi,%edx,4)");	// array->body[edx] = eax
    asm("iastore_st4_done:");
  }

#if defined(OPTIMIZE_INTERNAL_CODE) && (!defined(METAVM) || defined(METAVM_NO_ARRAY))
  CODE(opc_fst_fastore, fst_fastore, ST0, ST0, OPC_NONE) {
    asm("popl  %eax");		// eax = value
    asm("popl  %ecx\n\t"	// ecx = index
	"popl  %edx");		// edx = handle of array
    ARRAY_CHECK(%edx, %ecx, "fst_fastore");
    UNHAND(%edx, %edi);
#  ifdef USE_SSE2
    asm("movss %xmm0,(%edi,%ecx,4)");
#  else
    asm("fstps (%edi,%ecx,4)");
#  endif
  }
#endif	// OPTIMIZE_INTERNAL_CODE && !METAVM


  // aastore
	// compile: iastore1, fill_cache, array_check, aastore
  //
  // assumption: eax is obj_handle(handle).
  //
#ifdef RUNTIME_DEBUG
#  define AASTORE_TEST_DEBUG \
  asm("pushl %eax");\
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl %edi\n\tpushl %ecx\n\tpushl %edx");\
    PUSH_CONSTSTR("  edx: 0x%08x, ecx: 0x%08x, edi: 0x%08x\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $16,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#else
#  define AASTORE_TEST_DEBUG
#endif

#if defined(METAVM) && !defined(METAVM_NO_ARRAY)
#  define METAVM_AASTORE(HANDLE) \
    OBJ_LENGTH(HANDLE, %edi)	/* edi = obj_length(obj) */
	// JUMP_IF_NOT_* in METAVM_ASTORE break edi serving obj_length()
#else
#  define METAVM_AASTORE(HANDLE)
#endif	// METAVM_NO_ARRAY

#ifndef NO_NULL_AND_ARRAY_CHECK
#  define AASTORE_TEST(OBJ, HANDLE, STATE) \
    asm("pushl %eax");\
    FUNCCALL_IN(2);\
    asm("pushl %0" : : "m" (ee));	/* push ee */\
    AASTORE_TEST_DEBUG;\
    asm("pushl (" #OBJ ",%edi,4)\n\t"\
				/* push array->body[obj_length(..)] */\
	"pushl %eax\n\t"		/* push value */\
	/* call is_instance_of(value, array->body[eax], ee) */\
	"call  " FUNCTION(is_instance_of));\
    CAST_UINT8_TO_INT32(%eax);\
    asm("addl  $12,%esp");\
	/* if (eax)  goto ... */\
    asm("testl %eax,%eax");\
    FUNCCALL_OUT(2);\
    asm("popl  %eax");\
    \
    asm("jnz   aastore_st" #STATE "_1");\
    SIGNAL_ERROR0(EXCID_ArrayStoreException);\
    asm("aastore_st" #STATE "_1:")
#else
#  define AASTORE_TEST(OBJ, HANDLE, STATE)
#endif	// NO_NULL_AND_ARRAY_CHECK

  CODE(opc_aastore, aastore, ST2, ST0, OPC_THROW) {
    METAVM_ASTORE(%edx, %ecx, %eax, "aastore_st2", 2);
    METAVM_AASTORE(%edx);		// edi = obj_length(%edx)
    UNHAND(%edx, %edx);
    AASTORE_TEST(%edx, %edx, 2);
    asm("movl  %eax,(%edx,%ecx,4)");	// array->body[ecx] = eax
    asm("aastore_st2_done:");
  }
  CODE(opc_aastore, aastore, ST4, ST0, OPC_THROW) {
    METAVM_ASTORE(%ecx, %edx, %eax, "aastore_st4", 4);
    METAVM_AASTORE(%ecx);		// edi = obj_length(%edx)
    UNHAND(%ecx, %ecx);
    AASTORE_TEST(%ecx, %ecx, 4);
    asm("movl  %eax,(%ecx,%edx,4)");	// array->body[ecx] = eax
    asm("aastore_st4_done:");
  }


  // lastore
	// compile: fill_cache, lastore
#ifdef METAVM
#  define METAVM_WRITE2(HANDLE, SLOT, VAL_LOW, VAL_HIGH, LABEL, STATE, FUNC) \
    asm("pushl %eax\n\tpushl %edi");	/* save */\
    asm("movl  " #HANDLE ",%eax");	/* eax = handle */\
    JUMP_IF_NOT_PROXY(%eax, LABEL "_local");	/* 1st arg must not be %edi */\
    JUMP_IF_NOT_REMOTE(LABEL "_local");\
    asm("popl  %edi\n\tpopl  %eax");	/* restore */\
    \
    FUNCCALL_IN(STATE);\
    \
    asm("pushl " #VAL_HIGH "\n\t"/* value */\
	"pushl " #VAL_LOW "\n\t"/* value */\
	"pushl " #SLOT "\n\t"	/* slot */\
	"pushl " #HANDLE);	/* obj (Proxy) */\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    \
    asm("call  " FUNCTION(FUNC) "\n\t"\
	"popl  %edi\n\t"	/* edi = ee */\
	"addl  $16,%esp");\
    \
    FUNCCALL_OUT(STATE);\
    \
    JUMP_IF_EXC_HASNT_OCCURRED(%edi /* is ee */, LABEL "_done");\
DEBUG_IN;\
PUSH_CONSTSTR("METAVM_WRITE2 exc. occurred.\n");\
asm("call  " FUNCTION(printf) "\n\t"\
    "addl  $4,%esp");\
FFLUSH;\
DEBUG_OUT;\
    SIGNAL_ERROR_JUMP();\
    \
    asm(LABEL "_local:\n\t"\
	"popl  %edi\n\tpopl  %eax")		/* restore */
#  define METAVM_PUTFIELD2(HANDLE, SLOT, VAL_LOW, VAL_HIGH, LABEL, STATE) \
   METAVM_WRITE2(HANDLE, SLOT, VAL_LOW, VAL_HIGH, LABEL, STATE, proxy_put64field)
#else
#  define METAVM_PUTFIELD2(HANDLE, SLOT, VAL_LOW, VAL_HIGH, LABEL, STATE)
#endif	// METAVM

#if defined(METAVM) && !defined(METAVM_NO_ARRAY)
#  define METAVM_ASTORE2(HANDLE, SLOT, VAL_LOW, VAL_HIGH, LABEL, STATE) \
   METAVM_WRITE2(HANDLE, SLOT, VAL_LOW, VAL_HIGH, LABEL, STATE, proxy_astore64)
#else
#  define METAVM_ASTORE2(HANDLE, SLOT, VAL_LOW, VAL_HIGH, LABEL, STATE)
#endif	// METAVM_NO_ARRAY

#define CODE_LASTORE(OPTOP1_REG, OPTOP2_REG, STATE) \
  CODE(opc_lastore, [ld]astore, ST##STATE, ST0, OPC_SIGNAL) {\
    asm("popl  %eax\n\t"	/* index */\
	"movl  (%esp),%edi");	/* handle */\
    METAVM_ASTORE2(%edi, %eax, OPTOP1_REG, OPTOP2_REG, "lastore_st" #STATE, STATE);\
    ARRAY_CHECK(%edi, %eax, "lastore_st" #STATE);\
    asm("popl  %edi");\
    UNHAND(%edi, %edi);\
    asm("leal  (%edi,%eax,8),%edi\n\t"\
	"movl  " #OPTOP1_REG ",(%edi)\n\t"\
	"movl  " #OPTOP2_REG ",4(%edi)");\
    asm("lastore_st" #STATE "_done:");\
    VALUE64_DEBUG(OPTOP1_REG, OPTOP2_REG);\
  }

  CODE_LASTORE(%ecx, %edx, 2);
  CODE_LASTORE(%edx, %ecx, 4);

#if defined(OPTIMIZE_INTERNAL_CODE) && (!defined(METAVM) || defined(METAVM_NO_ARRAY))
  CODE(opc_dst_dastore, dst_dastore, ST0, ST0, OPC_SIGNAL) {
    asm("movl  8(%esp),%eax\n\t"
	"movl  12(%esp),%edi");
    ARRAY_CHECK(%edi, %eax, "dst_dastore");
    asm("movl  12(%esp),%edi");
    UNHAND(%edi, %edi);
    asm("leal  (%edi,%eax,8),%edi");
#  ifdef USE_SSE2
    asm("movsd %xmm0,(%edi)");
#  else
    asm("fstpl (%edi)");
#  endif
    asm("addl  $16,%esp");
  }
#endif	// OPTIMIZE_INTERNAL_CODE && !METAVM


  // bastore
	// compile: iastore1, fill_cache, array_check, bastore
  CODE(opc_bastore, bastore, ST2, ST0, OPC_THROW) {
    METAVM_ASTORE(%edx, %ecx, %eax, "bastore_st2", 2);
    UNHAND(%edx, %edi);
    asm("movb  %al,(%edi,%ecx)");
    asm("bastore_st2_done:");
    VALUE_DEBUG(%eax);
  }
  CODE(opc_bastore, bastore, ST4, ST0, OPC_THROW) {
    METAVM_ASTORE(%ecx, %edx, %eax, "bastore_st4", 4);
    UNHAND(%ecx, %edi);
    asm("movb  %al,(%edi,%edx)");
    asm("bastore_st4_done:");
    VALUE_DEBUG(%eax);
  }


  // castore
	// compile: iastore1, fill_cache, array_check, castore
  CODE(opc_castore, castore, ST2, ST0, OPC_THROW) {
    METAVM_ASTORE(%edx, %ecx, %eax, "castore_st2", 2);
    UNHAND(%edx, %edi);
    asm("movw  %ax,(%edi,%ecx,2)");
    asm("castore_st2_done:");
    VALUE_DEBUG(%eax);
  }
  CODE(opc_castore, castore, ST4, ST0, OPC_THROW) {
    METAVM_ASTORE(%ecx, %edx, %eax, "castore_st4", 4);
    UNHAND(%ecx, %edi);
    asm("movw  %ax,(%edi,%edx,2)");
    asm("castore_st4_done:");
    VALUE_DEBUG(%eax);
  }


  // sastore
	// compile: iastore1, fill_cache, array_check, sastore
  CODE(opc_sastore, sastore, ST2, ST0, OPC_THROW) {
    METAVM_ASTORE(%edx, %ecx, %eax, "sastore_st2", 2);
    UNHAND(%edx, %edi);
    asm("movw  %ax,(%edi,%ecx,2)");
    asm("sastore_st2_done:");
    VALUE_DEBUG(%eax);
  }
  CODE(opc_sastore, sastore, ST4, ST0, OPC_THROW) {
    METAVM_ASTORE(%ecx, %edx, %eax, "sastore_st4", 4);
    UNHAND(%ecx, %edi);
    asm("movw  %ax,(%edi,%edx,2)");
    asm("sastore_st4_done:");
    VALUE_DEBUG(%eax);
  }


  // pop
  CODE(opc_pop, pop, ST0, ST0, OPC_NONE) {
    asm("addl  $4,%esp");
  }
  CODE(opc_pop, pop, ST1, ST0, OPC_NONE) {}
  CODE(opc_pop, pop, ST2, ST1, OPC_NONE) {}
  CODE(opc_pop, pop, ST3, ST0, OPC_NONE) {}
  CODE(opc_pop, pop, ST4, ST3, OPC_NONE) {}


  // pop2
  CODE(opc_pop2, pop2, ST0, ST0, OPC_NONE) {
    asm("addl  $8,%esp");
  }
  CODE(opc_pop2, pop2, ST1, ST0, OPC_NONE) {
    asm("addl  $4,%esp");
  }
  CODE(opc_pop2, pop2, ST2, ST0, OPC_NONE) {}
  CODE(opc_pop2, pop2, ST3, ST0, OPC_NONE) {
    asm("addl  $4,%esp");
  }
  CODE(opc_pop2, pop2, ST4, ST0, OPC_NONE) {}


  // dup
  CODE(opc_dup, dup, ST0, ST1, OPC_NONE) {
    asm("movl  (%esp),%edx");
    VALUE_DEBUG(%edx);
  }
  CODE(opc_dup, dup, ST1, ST2, OPC_NONE) {
    asm("movl  %edx,%ecx");
    VALUE_DEBUG(%ecx);
  }
  CODE(opc_dup, dup, ST2, ST2, OPC_NONE) {
    asm("pushl %edx\n\t"
	"movl  %ecx,%edx");
    VALUE_DEBUG(%edx);
  }
  CODE(opc_dup, dup, ST3, ST2, OPC_NONE) {
    asm("movl  %ecx,%edx");
    VALUE_DEBUG(%edx);
  }
  CODE(opc_dup, dup, ST4, ST2, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"movl  %edx,%ecx");
    VALUE_DEBUG(%ecx);
  }


  // dup_x1
  CODE(opc_dup_x1, dup_x1, ST0, ST2, OPC_NONE) {
#if 0
    asm("popl  %ecx\n\t"		// now state 3
	"movl  (%esp),%edx\n\t"
	"movl  %ecx,(%esp)");
#else
    asm("popl  %ecx\n\t"		// now state 3
	"popl  %edx\n\t"
	"pushl %ecx");
#endif
  }
  CODE(opc_dup_x1, dup_x1, ST1, ST4, OPC_NONE) {
#if 0
    asm("movl  (%esp),%ecx\n\t"
	"movl  %edx,(%esp)");
#else
    asm("popl  %ecx\n\t"
	"pushl %edx");
#endif
  }
  CODE(opc_dup_x1, dup_x1, ST2, ST2, OPC_NONE) {
    asm("pushl %ecx");
  }
  CODE(opc_dup_x1, dup_x1, ST3, ST2, OPC_NONE) {
#if 0
    asm("movl  (%esp),%edx\n\t"
	"movl  %ecx,(%esp)");
#else
    asm("popl  %edx\n\t"
	"pushl %ecx");
#endif
  }
  CODE(opc_dup_x1, dup_x1, ST4, ST4, OPC_NONE) {
    asm("pushl %edx");
  }


  // dup_x2
#if 0
#define DUP_X2_ST24(OPTOP1_REG) \
    asm("pushl (%esp)\n\t"\
	"movl  " #OPTOP1_REG ",4(%esp)")
#else
#define DUP_X2_ST24(OPTOP1_REG) \
    asm("popl  %eax\n\t"\
	"pushl " #OPTOP1_REG "\n\t"\
	"pushl %eax")
#endif

  CODE(opc_dup_x2, dup_x2, ST0, ST2, OPC_NONE) {
    asm("popl  %ecx\n\t"
	"popl  %edx");		// now state 2
    DUP_X2_ST24(%ecx);
  }
  CODE(opc_dup_x2, dup_x2, ST1, ST4, OPC_NONE) {
    asm("popl  %ecx");		// now state 4
    DUP_X2_ST24(%edx);
  }
  CODE(opc_dup_x2, dup_x2, ST2, ST2, OPC_NONE) {
    DUP_X2_ST24(%ecx);
  }
  CODE(opc_dup_x2, dup_x2, ST3, ST2, OPC_NONE) {
    asm("popl  %edx");		// now state 2
    DUP_X2_ST24(%ecx);
  }
  CODE(opc_dup_x2, dup_x2, ST4, ST4, OPC_NONE) {
    DUP_X2_ST24(%edx);
  }


  // dup2
  CODE(opc_dup2, dup2, ST0, ST2, OPC_NONE) {
#if 0
    asm("movl  (%esp),%ecx\n\t"
	"movl  4(%esp),%edx");
#else
    asm("popl  %ecx\n\t"
	"popl  %edx\n\t"
	"subl  $8,%esp");
#endif
  }
  CODE(opc_dup2, dup2, ST1, ST4, OPC_NONE) {
    asm("movl  (%esp),%ecx\n\t"
	"pushl %edx");
  }
  CODE(opc_dup2, dup2, ST2, ST2, OPC_NONE) {
    asm("pushl %edx\n\t"
	"pushl %ecx");
  }
  CODE(opc_dup2, dup2, ST3, ST2, OPC_NONE) {
    asm("movl  (%esp),%edx\n\t"
	"pushl %ecx");
  }
  CODE(opc_dup2, dup2, ST4, ST4, OPC_NONE) {
    asm("pushl %ecx\n\t"
	"pushl %edx");
  }


  // dup2_x1
#define DUP2_X1_ST24(OPTOP1_REG, OPTOP2_REG) \
    asm("popl  %eax\n\t"		/* eax = optop[-3] */\
	"pushl " #OPTOP2_REG "\n\t"	/* optop[-5] = optop[-2] */\
	"pushl " #OPTOP1_REG "\n\t"	/* optop[-4] = optop[-1] */\
	"pushl %eax")		/* optop[-3] = eax */

  CODE(opc_dup2_x1, dup2_x1, ST0, ST2, OPC_NONE) {
    asm("popl  %ecx\n\t"
	"popl  %edx");	// now state 2
    DUP2_X1_ST24(%ecx, %edx);
  }
  CODE(opc_dup2_x1, dup2_x1, ST1, ST4, OPC_NONE) {
    asm("popl  %ecx");	// now state 4
    DUP2_X1_ST24(%edx, %ecx);
  }
  CODE(opc_dup2_x1, dup2_x1, ST2, ST2, OPC_NONE) {
    DUP2_X1_ST24(%ecx, %edx);
  }
  CODE(opc_dup2_x1, dup2_x1, ST3, ST2, OPC_NONE) {
    asm("popl  %edx");	// now state 2
    DUP2_X1_ST24(%ecx, %edx);
  }
  CODE(opc_dup2_x1, dup2_x1, ST4, ST4, OPC_NONE) {
    DUP2_X1_ST24(%edx, %ecx);
  }


  // dup2_x2
#define DUP2_X2_ST24(OPTOP1_REG, OPTOP2_REG) \
    asm("popl  %eax\n\t"		/* eax = optop[-3] */\
	"popl  %edi\n\t"		/* edi = optop[-4] */\
	"pushl " #OPTOP2_REG "\n\t"	/* optop[-4] = optop[-2] */\
	"pushl " #OPTOP1_REG "\n\t"	/* optop[-3] = optop[-1] */\
	"pushl %edi\n\t"		/* optop[-2] = edi */\
	"pushl %eax");			/* optop[-1] = eax */

  CODE(opc_dup2_x2, dup2_x2, ST0, ST2, OPC_NONE) {
    asm("popl  %ecx\n\t"
	"popl  %edx");	// now state 2
    DUP2_X2_ST24(%ecx, %edx);
  }
  CODE(opc_dup2_x2, dup2_x2, ST1, ST4, OPC_NONE) {
    asm("popl  %ecx");	// now state 4
    DUP2_X2_ST24(%edx, %ecx);
  }
  CODE(opc_dup2_x2, dup2_x2, ST2, ST2, OPC_NONE) {
    DUP2_X2_ST24(%ecx, %edx);
  }
  CODE(opc_dup2_x2, dup2_x2, ST3, ST2, OPC_NONE) {
    asm("popl  %edx");	// now state 2
    DUP2_X2_ST24(%ecx, %edx);
  }
  CODE(opc_dup2_x2, dup2_x2, ST4, ST4, OPC_NONE) {
    DUP2_X2_ST24(%edx, %ecx);
  }


  // swap
  CODE(opc_swap, swap, ST0, ST4, OPC_NONE) {
    asm("popl  %ecx\n\t"
	"popl  %edx");	// now state 2
  }
  CODE(opc_swap, swap, ST1, ST2, OPC_NONE) {
    asm("popl  %ecx");	// now state 4
  }
  CODE(opc_swap, swap, ST2, ST4, OPC_NONE) {}
  CODE(opc_swap, swap, ST3, ST4, OPC_NONE) {
    asm("popl  %edx");	// now state 2
  }
  CODE(opc_swap, swap, ST4, ST2, OPC_NONE) {}


  // iadd, isub, imul, iand, ior, ixor
#ifdef RUNTIME_DEBUG
#  define ARITH_INT_DEBUG1(VAL1, VAL2, SYM) \
  asm("pushl %eax");\
  if (runtime_debug) {		/* break eax */	\
    asm("movl  (%esp),%eax");	/* restore */	\
    DEBUG_IN;\
    asm("pushl " #VAL2 "\n\t"\
	"pushl " #VAL1);\
    PUSH_CONSTSTR("  %d " SYM " %d\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $12,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#else
#  define ARITH_INT_DEBUG1(VAL1, VAL2, SYM)
#endif

#define CODE_ARITH_INT(VOP, ROP, SYM) \
  CODE(opc_i##VOP, i##VOP, ST0, ST1, OPC_NONE) {\
    asm("popl  %ecx\n\t"\
	"popl  %edx");	/* now state 2 */\
    ARITH_INT_DEBUG1(%edx, %ecx, SYM);\
    asm(#ROP "l  %ecx,%edx");\
  }\
  CODE(opc_i##VOP, i##VOP, ST1, ST3, OPC_NONE) {\
    asm("popl  %ecx");	/* now state 4 */\
    ARITH_INT_DEBUG1(%ecx, %edx, SYM);\
    asm(#ROP "l  %edx,%ecx");\
  }\
  CODE(opc_i##VOP, i##VOP, ST2, ST1, OPC_NONE) {\
    ARITH_INT_DEBUG1(%edx, %ecx, SYM);\
    asm(#ROP "l  %ecx,%edx");\
  }\
  CODE(opc_i##VOP, i##VOP, ST3, ST1, OPC_NONE) {\
    asm("popl  %edx");	/* now state 2 */\
    ARITH_INT_DEBUG1(%edx, %ecx, SYM);\
    asm(#ROP "l  %ecx,%edx");\
  }\
  CODE(opc_i##VOP, i##VOP, ST4, ST3, OPC_NONE) {\
    ARITH_INT_DEBUG1(%ecx, %edx, SYM);\
    asm(#ROP "l  %edx,%ecx");\
  }

  CODE_ARITH_INT(add, add, "+");
  CODE_ARITH_INT(sub, sub, "-");
  CODE_ARITH_INT(mul, imul, "*");
  CODE_ARITH_INT(and, and, "&");
  CODE_ARITH_INT(or, or, "|");
  CODE_ARITH_INT(xor, xor, "^");


  // idiv, irem
#ifdef ARITHEXC_BY_SIGNAL
#  define INT_TEST(VOP, LABEL, DIVISOR)
#else
#  define INT_TEST(VOP, LABEL, DIVISOR)	/* dividend is %eax */\
    asm("testl " #DIVISOR "," #DIVISOR "\n\t"\
	"jnz   " LABEL "_1");\
    SIGNAL_ERROR0(EXCID_ArithmeticException);\
    asm(LABEL "_1:");\
    asm("cmpl  $-1," #DIVISOR "\n\t"\
	"jne   " LABEL "_3\n\t"\
	"cmpl  $0x80000000,%eax\n\t"\
	"jne   " LABEL "_3\n\t"\
	/* %eax,%edx must be 0x80000000,0 */\
	"xorl  %edx,%edx\n\t"\
	"jmp   " LABEL "_done");\
    asm(LABEL "_3:");
#endif

#define CODE_ARITH_INT_TEST(VOP, RESULT_REG, SYM) \
  CODE(opc_i##VOP, i##VOP, ST0, ST3, OPC_SIGNAL) {\
    asm("popl  %ecx");	/* now state 3 */\
    asm("popl  %eax");\
    ARITH_INT_DEBUG1(%eax, %ecx, SYM);\
    INT_TEST(VOP, "i" #VOP "_st0", %ecx);\
    asm("movl  %eax,%edx\n\tsarl  $31,%edx");	/* instead of `cdq' */\
    asm("idivl %ecx\n\t"\
      "i" #VOP "_st0_done:"\
	"movl  " #RESULT_REG ",%ecx");\
  }\
  CODE(opc_i##VOP, i##VOP, ST1, ST3, OPC_SIGNAL) {\
    asm("popl  %eax\n\t"\
	"movl  %edx,%ecx");\
    ARITH_INT_DEBUG1(%eax, %ecx, SYM);\
    INT_TEST(VOP, "i" #VOP "_st1", %ecx);\
    asm("movl  %eax,%edx\n\tsarl  $31,%edx");	/* instead of `cdq' */\
    asm("idivl %ecx\n\t"\
      "i" #VOP "_st1_done:"\
	"movl  " #RESULT_REG ",%ecx");\
  }\
  CODE(opc_i##VOP, i##VOP, ST2, ST3, OPC_SIGNAL) {\
    asm("movl  %edx,%eax");\
    ARITH_INT_DEBUG1(%eax, %ecx, SYM);\
    INT_TEST(VOP, "i" #VOP "_st2", %ecx);\
    asm("movl  %eax,%edx\n\tsarl  $31,%edx");	/* instead of `cdq' */\
    asm("idivl %ecx\n\t"\
      "i" #VOP "_st2_done:"\
	"movl  " #RESULT_REG ",%ecx");\
  }\
  CODE(opc_i##VOP, i##VOP, ST3, ST3, OPC_SIGNAL) {\
    asm("popl  %eax");\
    ARITH_INT_DEBUG1(%eax, %ecx, SYM);\
    INT_TEST(VOP, "i" #VOP "_st3", %ecx);\
    asm("movl  %eax,%edx\n\tsarl  $31,%edx");	/* instead of `cdq' */\
    asm("idivl %ecx\n\t"	/* %eax ... %edx = %edx:%eax / %ecx */\
      "i" #VOP "_st3_done:"\
	"movl  " #RESULT_REG ",%ecx");\
  }\
  CODE(opc_i##VOP, i##VOP, ST4, ST3, OPC_SIGNAL) {\
    asm("movl  %ecx,%eax\n\t"\
	"movl  %edx,%ecx");\
    ARITH_INT_DEBUG1(%eax, %ecx, SYM);\
    INT_TEST(VOP, "i" #VOP "_st4", %ecx);\
    asm("movl  %eax,%edx\n\tsarl  $31,%edx");	/* instead of `cdq' */\
    asm("idivl %ecx\n\t"\
      "i" #VOP "_st4_done:"\
	"movl  " #RESULT_REG ",%ecx");\
  }

  CODE_ARITH_INT_TEST(div, %eax, "/");
  CODE_ARITH_INT_TEST(rem, %edx, "%%");


  // ladd, lsub, land, lor, lxor
#ifdef RUNTIME_DEBUG
#  define ARITH_LONG_DEBUG1(VAL1_LOW, VAL1_HIGH, VAL2_LOW, VAL2_HIGH, SYM) \
  asm("pushl %eax");\
  if (runtime_debug) {		/* break eax */ \
    asm("movl  (%esp),%eax");	/* restore */ \
    DEBUG_IN;\
    asm("pushl " #VAL2_HIGH "\n\tpushl " #VAL2_LOW "\n\t"\
	"pushl " #VAL2_HIGH "\n\tpushl " #VAL2_LOW "\n\t"\
	"pushl " #VAL1_HIGH "\n\tpushl " #VAL1_LOW "\n\t"\
	"pushl " #VAL1_HIGH "\n\tpushl " #VAL1_LOW);\
    PUSH_CONSTSTR("  %lld(0x%016llx) " SYM " %lld(0x%016llx)\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $36,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#else
#  define ARITH_LONG_DEBUG1(VAL1_LOW, VAL1_HIGH, VAL2_LOW, VAL2_HIGH, SYM)
#endif

#define ARITH_LONG_SUB(ROP1, ROP2, OPTOP1_REG, OPTOP2_REG, SYM) \
    asm("popl  " #OPTOP1_REG "\n\t"	/* val1[0:31] */\
	"popl  " #OPTOP2_REG);		/* val2[32:63] */\
    ARITH_LONG_DEBUG1(OPTOP1_REG, OPTOP2_REG, %eax, %edi, SYM);\
    asm(#ROP1 "l  %eax," #OPTOP1_REG "\n\t"	/* [0:31] */\
	#ROP2 "l  %edi," #OPTOP2_REG)		/* [32:63] */

#define CODE_ARITH_LONG_SUB(VOP, ROP1, ROP2, SYM) \
  CODE(opc_l##VOP, l##VOP, ST0, ST2, OPC_NONE) {\
    asm("popl  %eax\n\t"\
	"popl  %edi");\
    ARITH_LONG_SUB(ROP1, ROP2, %ecx, %edx, SYM);\
  }\
  CODE(opc_l##VOP, l##VOP, ST1, ST4, OPC_NONE) {\
    asm("movl  %edx,%eax\n\t"\
	"popl  %edi");\
    ARITH_LONG_SUB(ROP1, ROP2, %edx, %ecx, SYM);\
  }\
  CODE(opc_l##VOP, l##VOP, ST2, ST2, OPC_NONE) {\
    asm("movl  %ecx,%eax\n\t"\
	"movl  %edx,%edi");\
    ARITH_LONG_SUB(ROP1, ROP2, %ecx, %edx, SYM);\
  }\
  CODE(opc_l##VOP, l##VOP, ST3, ST2, OPC_NONE) {\
    asm("movl  %ecx,%eax\n\t"\
	"popl  %edi");\
    ARITH_LONG_SUB(ROP1, ROP2, %ecx, %edx, SYM);\
  }\
  CODE(opc_l##VOP, l##VOP, ST4, ST4, OPC_NONE) {\
    asm("movl  %edx,%eax\n\t"\
	"movl  %ecx,%edi");\
    ARITH_LONG_SUB(ROP1, ROP2, %edx, %ecx, SYM);\
  }

#define ARITH_LONG(ROP1, ROP2, OPTOP1_REG, OPTOP2_REG, SYM) \
    asm("popl  %eax\n\t"	/* eax = val1[0:31] */\
	"popl  %edi");		/* edi = val2[32:63] */\
    ARITH_LONG_DEBUG1(%eax, %edi, OPTOP1_REG, OPTOP2_REG, SYM);\
    asm(#ROP1 "l  %eax," #OPTOP1_REG "\n\t"	/* [0:31] */\
	#ROP2 "l  %edi," #OPTOP2_REG)		/* [32:63] */

#define CODE_ARITH_LONG(VOP, ROP1, ROP2, SYM) \
  CODE(opc_l##VOP, l##VOP, ST0, ST2, OPC_NONE) {\
    asm("popl  %ecx\n\t"\
	"popl  %edx");		/* now state 2 */\
    ARITH_LONG(ROP1, ROP2, %ecx, %edx, SYM);\
  }\
  CODE(opc_l##VOP, l##VOP, ST1, ST4, OPC_NONE) {\
    asm("popl  %ecx");		/* now state 4 */\
    ARITH_LONG(ROP1, ROP2, %edx, %ecx, SYM);\
  }\
  CODE(opc_l##VOP, l##VOP, ST2, ST2, OPC_NONE) {\
    ARITH_LONG(ROP1, ROP2, %ecx, %edx, SYM);\
  }\
  CODE(opc_l##VOP, l##VOP, ST3, ST2, OPC_NONE) {\
    asm("popl  %edx");		/* now state 2 */\
    ARITH_LONG(ROP1, ROP2, %ecx, %edx, SYM);\
  }\
  CODE(opc_l##VOP, l##VOP, ST4, ST4, OPC_NONE) {\
    ARITH_LONG(ROP1, ROP2, %edx, %ecx, SYM);\
  }

  CODE_ARITH_LONG(add, add, adc, "+");
  CODE_ARITH_LONG_SUB(sub, sub, sbb, "-");
  CODE_ARITH_LONG(and, and, and, "&");
  CODE_ARITH_LONG(or, or, or, "|");
  CODE_ARITH_LONG(xor, xor, xor, "^");


  // lmul
#ifdef RUNTIME_DEBUG
#  define ARITH_LONG_CALL_DEBUG1(OPTOP1_REG, OPTOP2_REG, SYM) \
  asm("pushl %eax\n\tpushl %edi");\
  if (runtime_debug) {\
    asm("movl  8(%esp),%eax\n\t"\
	"movl  12(%esp),%edi");\
    DEBUG_IN;\
    asm("pushl " #OPTOP2_REG "\n\t"\
	"pushl " #OPTOP1_REG "\n\t"\
	"pushl %edi\n\tpushl %eax");\
    PUSH_CONSTSTR("  %lld " SYM " %lld\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $20,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl %edi\n\tpopl %eax")
#else
#  define ARITH_LONG_CALL_DEBUG1(OPTOP1_REG, OPTOP2_REG, SYM)
#endif

#define LMUL_ST24(OPTOP1, LOW_WORD, HIGH_WORD) \
    asm("movl  " #OPTOP1 ",%eax\n\t"\
	"movl  %edx,%edi\n\t"\
	"mull  (%esp)\n\t"\
	"movl  %eax,-8(%esp)\n\t"\
	"movl  %edx,-4(%esp)\n\t"\
	"imull 4(%esp)," #LOW_WORD "\n\t"\
	"addl  " #LOW_WORD ",-4(%esp)\n\t"\
	"imull (%esp)," #HIGH_WORD "\n\t"\
	"addl  " #HIGH_WORD ",-4(%esp)\n\t"\
	"movl  -8(%esp),%ecx\n\t"\
	"movl  -4(%esp),%edx\n\t"	/* now state 2 */\
	"addl  $8,%esp")

  CODE(opc_lmul, lmul, ST0, ST2, OPC_NONE) {
    asm("popl  %ecx\n\t"
	"popl  %edx");	// now state 2
    ARITH_LONG_CALL_DEBUG1(%ecx, %edx, "*");
    LMUL_ST24(%ecx, %ecx, %edi);
  }
  CODE(opc_lmul, lmul, ST1, ST2, OPC_NONE) {
    asm("popl  %ecx");	// now state 4
    ARITH_LONG_CALL_DEBUG1(%edx, %ecx, "*");
    LMUL_ST24(%edx, %edi, %ecx);
  }
  CODE(opc_lmul, lmul, ST2, ST2, OPC_NONE) {
    ARITH_LONG_CALL_DEBUG1(%ecx, %edx, "*");
    LMUL_ST24(%ecx, %ecx, %edi);
  }
  CODE(opc_lmul, lmul, ST3, ST2, OPC_NONE) {
    asm("popl  %edx");	// now state 2
    ARITH_LONG_CALL_DEBUG1(%ecx, %edx, "*");
    LMUL_ST24(%ecx, %ecx, %edi);
  }
  CODE(opc_lmul, lmul, ST4, ST2, OPC_NONE) {
    ARITH_LONG_CALL_DEBUG1(%edx, %ecx, "*");
    LMUL_ST24(%edx, %edi, %ecx);
  }


  // ldiv, lrem
#define ARITH_LONG_CALL_ST24(ROP, OPTOP1_REG, OPTOP2_REG, SYM) \
    ARITH_LONG_CALL_DEBUG1(OPTOP1_REG, OPTOP2_REG, SYM);\
    asm("popl  %eax\n\t"	/* eax = v1[0:31] */\
	"popl  %edi");	/* edi = v1[32:63] */\
    FUNCCALL_IN(0);\
    /* back up for signal handler */\
    asm("pushl %esi\n\t"\
	"pushl %ebx\n\t"\
	"pushl %ebp\n\t");\
    asm("pushl " #OPTOP2_REG "\n\t"	/* push v2[32:63] */\
	"pushl " #OPTOP1_REG "\n\t"	/* push v2[0:31] */\
	"pushl %edi\n\t"	/* push v1[32:63] */\
	"pushl %eax\n\t"	/* push v1[0:31] */\
	"call  " ROP "\n\t"\
	"addl  $28,%esp");\
    asm(/* movl %edx,%edx */\
	"movl  %eax,%ecx");\
    FUNCCALL_OUT(0)
	// now state 2

#ifdef ARITHEXC_BY_SIGNAL
#  define LONG_TEST_ST24(VOP, LABEL)
#else
#  define LONG_TEST_ST24(VOP, LABEL) \
	/* if ((%edx == 0) && (%ecx == 0))  throw ArithmeticException; */\
    asm("movl  %edx,%eax\n\t"\
	"orl   %ecx,%eax\n\t"\
	"jnz   " LABEL "_done");\
    SIGNAL_ERROR0(EXCID_ArithmeticException);\
    asm(LABEL "_done:")
	// now state [24]
#endif

#define CODE_ARITH_LONG_TEST(VOP, ROP, SYM) \
  CODE(opc_l##VOP, l##VOP, ST0, ST2, OPC_SIGNAL) {\
    asm("popl  %ecx\n\t"\
	"popl  %edx");	/* now state 2 */\
    LONG_TEST_ST24(VOP, "l" #VOP "_st0");\
    ARITH_LONG_CALL_ST24(ROP, %ecx, %edx, SYM);\
  }\
  CODE(opc_l##VOP, l##VOP, ST1, ST2, OPC_SIGNAL) {\
    asm("popl  %ecx");	/* now state 4 */\
    LONG_TEST_ST24(VOP, "l" #VOP "_st1");\
    ARITH_LONG_CALL_ST24(ROP, %edx, %ecx, SYM);\
  }\
  CODE(opc_l##VOP, l##VOP, ST2, ST2, OPC_SIGNAL) {\
    LONG_TEST_ST24(VOP, "l" #VOP "_st2");\
    ARITH_LONG_CALL_ST24(ROP, %ecx, %edx, SYM);\
  }\
  CODE(opc_l##VOP, l##VOP, ST3, ST2, OPC_SIGNAL) {\
    asm("popl  %edx");	/* now state 2 */\
    LONG_TEST_ST24(VOP, "l" #VOP "_st3");\
    ARITH_LONG_CALL_ST24(ROP, %ecx, %edx, SYM);\
  }\
  CODE(opc_l##VOP, l##VOP, ST4, ST2, OPC_SIGNAL) {\
    LONG_TEST_ST24(VOP, "l" #VOP "_st4");\
    ARITH_LONG_CALL_ST24(ROP, %edx, %ecx, SYM);\
  }

#ifdef _WIN32
  CODE_ARITH_LONG_TEST(div, FUNCTION(_alldiv), "/");
  CODE_ARITH_LONG_TEST(rem, FUNCTION(_allrem), "mod");
#else
  CODE_ARITH_LONG_TEST(div, FUNCTION(__divdi3), "/");
  CODE_ARITH_LONG_TEST(rem, FUNCTION(__moddi3), "mod");
#endif


  // fadd, fsub, fmul, fdiv
	// compile: flush_cache, fld(4), f..., fst
	// fmul, fdiv and strictfp: flush_cache, strict_fprep fld(4),
	//		strict_fscdown, f..., strict_fscup, fst, strict_fsettle
#ifdef RUNTIME_DEBUG
#  define ARITH_FLOAT_DEBUG1 \
  if (runtime_debug) {\
    asm("flds  4(%esp)\n\t"\
	"flds  (%esp)");\
    DEBUG_IN;\
    asm("subl  $16,%esp\n\t"\
	"fstpl 8(%esp)\n\t"\
	"fstpl (%esp)");\
    PUSH_CONSTSTR("  %g, %g\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $20,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }
#  define ARITH_FLOAT_DEBUG2 \
  if (runtime_debug) {\
    asm("flds  (%esp)");\
    DEBUG_IN;\
    asm("subl  $8,%esp\n\t"\
	"fstpl (%esp)");\
    PUSH_CONSTSTR("  %g\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $12,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }
#else
#  define ARITH_FLOAT_DEBUG1
#  define ARITH_FLOAT_DEBUG2
#endif

  CODE(opc_fadd, fadd, ST0, ST0, OPC_NONE) {
    asm("addl  $4,%esp");
#ifdef USE_SSE2
    asm("addss (%esp),%xmm0");
#else
    asm("fadds (%esp)");
#endif
  }

  CODE(opc_fmul, fmul, ST0, ST0, OPC_NONE) {
    asm("addl  $4,%esp");
#ifdef USE_SSE2
    asm("mulss (%esp),%xmm0");
#else
    asm("fmuls (%esp)");
#endif
  }

  CODE(opc_fsub, fsub, ST0, ST0, OPC_NONE) {
#ifdef USE_SSE2
    asm("subss (%esp),%xmm0");
#else
    asm("fsubs (%esp)");
#endif
    asm("addl  $4,%esp");
  }

  CODE(opc_fdiv, fdiv, ST0, ST0, OPC_NONE) {
#ifdef USE_SSE2
    asm("divss (%esp),%xmm0");
#else
    asm("fdivs (%esp)");
#endif
    asm("addl  $4,%esp");
  }

#ifdef OPTIMIZE_INTERNAL_CODE
  CODE(opc_fadd, fadd, ST1, ST0, OPC_NONE) {
#  ifdef USE_SSE2
    asm("addss (%esp),%xmm0");
#  else
    asm("fadds (%esp)");
#  endif
  }

  CODE(opc_fmul, fmul, ST1, ST0, OPC_NONE) {
#  ifdef USE_SSE2
    asm("mulss (%esp),%xmm0");
#  else
    asm("fmuls (%esp)");
#  endif
  }
#endif


#ifndef USE_SSE2
#  ifdef STRICT_USE_FSCALE
#    ifdef STRICT_PRELOAD
#      define ARITH_FLOAT_SCALE_PREPARE	asm("fld   %st(1)")
#    else
#      ifdef STRICT_FSCALE_USE_FLOAT
#        define ARITH_FLOAT_SCALE_PREPARE \
    asm("flds  %0\n\t" : : "m" (single_scale_neg))
#      else
#        define ARITH_FLOAT_SCALE_PREPARE \
    asm("fildl %0\n\t" : : "m" (single_scale_neg))
#      endif	// STRICT_FSCALE_USE_FLOAT
#    endif	// STRICT_PRELOAD
#      define ARITH_FLOAT_SCALE_DOWN	asm("fscale")
#      define ARITH_FLOAT_SCALE_UP	asm("fxch\n\t"\
					    "fchs\n\t"\
					    "fxch\n\t"\
					    "fscale")
#      define ARITH_FLOAT_SCALE_SETTLE	asm("ffreep %st(0)")
#  else	// STRICT_USE_FSCALE
#      define ARITH_FLOAT_SCALE_PREPARE
#    ifdef STRICT_PRELOAD
#      define ARITH_FLOAT_SCALE_DOWN	asm("fmul  %st(4)")
#      define ARITH_FLOAT_SCALE_UP	asm("fmul  %st(3)")
#    else
#      define ARITH_FLOAT_SCALE_DOWN \
    asm("fldt  %0\n\t"\
	"fmulp" : : "m" (*single_scale_neg) : "edx","ecx","esi")
#      define ARITH_FLOAT_SCALE_UP \
    asm("fldt  %0\n\t"\
	"fmulp" : : "m" (*single_scale_pos) : "edx","ecx","esi")
#    endif	// STRICT_PRELOAD
#      define ARITH_FLOAT_SCALE_SETTLE
#  endif	// STRICT_USE_FSCALE

  CODE(opc_strict_fprep, strict_fprep, STANY, STSTA, OPC_NONE) {
    ARITH_FLOAT_SCALE_PREPARE;
  }
#endif	// !USE_SSE2

  CODE(opc_fld4, fld4, STANY, STSTA, OPC_NONE) {
    ARITH_FLOAT_DEBUG1;
#ifdef USE_SSE2
    asm("movss 4(%esp),%xmm0");
#else
    asm("flds  4(%esp)");
#endif
  }

  CODE(opc_fld, fld, STANY, STSTA, OPC_NONE) {
    ARITH_FLOAT_DEBUG1;
#ifdef USE_SSE2
    asm("movss (%esp),%xmm0");
#else
    asm("flds  (%esp)");
#endif
  }

#ifndef USE_SSE2
  CODE(opc_strict_fscdown, strict_fscdown, STANY, STSTA, OPC_NONE) {
    ARITH_FLOAT_SCALE_DOWN;
  }

  CODE(opc_strict_fscup, strict_fscup, STANY, STSTA, OPC_NONE) {
    ARITH_FLOAT_SCALE_UP;
  }
#endif	// !USE_SSE2

  CODE(opc_fst, fst, STANY, STSTA, OPC_NONE) {
#ifdef USE_SSE2
    asm("movss %xmm0,(%esp)");
#else
    asm("fstps (%esp)");
#endif
    ARITH_FLOAT_DEBUG2;
  }

#ifndef USE_SSE2
  CODE(opc_strict_fsettle, strict_fsettle, STANY, STSTA, OPC_NONE) {
    ARITH_FLOAT_SCALE_SETTLE;
  }
#endif


  // dadd, dsub, dmul, ddiv
	// compile: flush_cache, dld(8), d..., dst
	// dmul, ddiv and strictfp: flush_cache, strict_dprep dld(8),
	//		strict_dscdown, d..., strict_dscup, dst, strict_dsettle
#ifdef RUNTIME_DEBUG
#  define ARITH_DOUBLE_DEBUG1 \
  if (runtime_debug) {\
    asm("movl  %esp,%edi");\
    DEBUG_IN;\
    asm("pushl 4(%edi)\n\tpushl (%edi)\n\t"\
	"pushl 12(%edi)\n\tpushl 8(%edi)");\
    PUSH_CONSTSTR("  %g, %g\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $20,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }
#  define ARITH_DOUBLE_DEBUG2 \
  if (runtime_debug) {\
    asm("movl  (%esp),%edi\n\t"\
	"movl  4(%esp),%eax");\
    DEBUG_IN;\
    asm("pushl %eax\n\tpushl %edi");\
    PUSH_CONSTSTR("  %g\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $12,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }
#else
#  define ARITH_DOUBLE_DEBUG1
#  define ARITH_DOUBLE_DEBUG2
#endif

  CODE(opc_dadd, dadd, ST0, ST0, OPC_NONE) {
    asm("addl  $8,%esp");
#ifdef USE_SSE2
    asm("addsd (%esp),%xmm0");
#else
    asm("faddl (%esp)");
#endif
  }

  CODE(opc_dmul, dmul, ST0, ST0, OPC_NONE) {
    asm("addl  $8,%esp");
#ifdef USE_SSE2
    asm("mulsd (%esp),%xmm0");
#else
    asm("fmull (%esp)");
#endif
  }

  CODE(opc_dsub, dsub, ST0, ST0, OPC_NONE) {
#ifdef USE_SSE2
    asm("subsd (%esp),%xmm0");
#else
    asm("fsubl (%esp)");
#endif
    asm("addl  $8,%esp");
  }

  CODE(opc_ddiv, ddiv, ST0, ST0, OPC_NONE) {
#ifdef USE_SSE2
    asm("divsd (%esp),%xmm0");
#else
    asm("fdivl (%esp)");
#endif
    asm("addl  $8,%esp");
  }

#ifdef OPTIMIZE_INTERNAL_CODE
  CODE(opc_dadd, dadd, ST2, ST0, OPC_NONE) {
#  ifdef USE_SSE2
    asm("addsd (%esp),%xmm0");
#  else
    asm("faddl (%esp)");
#  endif
  }

  CODE(opc_dmul, dmul, ST2, ST0, OPC_NONE) {
#  ifdef USE_SSE2
    asm("mulsd (%esp),%xmm0");
#  else
    asm("fmull (%esp)");
#  endif
  }
#endif


#ifndef USE_SSE2
#  ifdef STRICT_USE_FSCALE
#    ifdef STRICT_PRELOAD
#      define ARITH_DOUBLE_SCALE_PREPARE	asm("fld   %st(0)")
#    else
#      ifdef STRICT_FSCALE_USE_FLOAT
#        define ARITH_DOUBLE_SCALE_PREPARE \
    asm("flds  %0\n\t" : : "m" (double_scale_neg) : "edx","ecx","esi")
#      else
#        define ARITH_DOUBLE_SCALE_PREPARE \
    asm("fildl %0\n\t" : : "m" (double_scale_neg) : "edx","ecx","esi")
#      endif // STRICT_FSCALE_USE_FLOAT
#    endif	// STRICT_PRELOAD
#      define ARITH_DOUBLE_SCALE_DOWN	asm("fscale")
#      define ARITH_DOUBLE_SCALE_UP	asm("fxch\n\t"\
					    "fchs\n\t"\
					    "fxch\n\t"\
					    "fscale")
#      define ARITH_DOUBLE_SCALE_SETTLE	asm("ffreep  %st(0)")
#  else
#      define ARITH_DOUBLE_SCALE_PREPARE
#    ifdef STRICT_PRELOAD
#      define ARITH_DOUBLE_SCALE_DOWN	asm("fmul  %st(2)")
#      define ARITH_DOUBLE_SCALE_UP	asm("fmul  %st(1)")
#    else
#      define ARITH_DOUBLE_SCALE_DOWN \
    asm("fldt  %0\n\t"\
        "fmulp" : : "m" (*double_scale_neg) : "edx","ecx","esi")
#      define ARITH_DOUBLE_SCALE_UP \
    asm("fldt  %0\n\t"\
        "fmulp" : : "m" (*double_scale_pos) : "edx","ecx","esi")
#    endif	// STRICT_PRELOAD
#      define ARITH_DOUBLE_SCALE_SETTLE
#  endif	// STRICT_USE_FSCALE

  CODE(opc_strict_dprep, strict_dprep, STANY, STSTA, OPC_NONE) {
    ARITH_DOUBLE_SCALE_PREPARE;
  }
#endif	// !USE_SSE2

  CODE(opc_dld8, dld8, STANY, STSTA, OPC_NONE) {
    ARITH_DOUBLE_DEBUG1;
#ifdef USE_SSE2
    asm("movsd 8(%esp),%xmm0");
#else
    asm("fldl  8(%esp)");
#endif
  }

  CODE(opc_dld, dld, STANY, STSTA, OPC_NONE) {
#ifdef USE_SSE2
    asm("movsd (%esp),%xmm0");
#else
    asm("fldl  (%esp)");
#endif
  }

#ifndef USE_SSE2
  CODE(opc_strict_dscdown, strict_dscdown, STANY, STSTA, OPC_NONE) {
    ARITH_DOUBLE_SCALE_DOWN;
  }

  CODE(opc_strict_dscup, strict_dscup, STANY, STSTA, OPC_NONE) {
    ARITH_DOUBLE_SCALE_UP;
  }
#endif	// !USE_SSE2

  CODE(opc_dst, dst, STANY, STSTA, OPC_NONE) {
#ifdef USE_SSE2
    asm("movsd %xmm0,(%esp)");
#else
    asm("fstpl (%esp)");
#endif
    ARITH_DOUBLE_DEBUG2;
  }

#ifndef USE_SSE2
  CODE(opc_strict_dsettle, strict_dsettle, STANY, STSTA, OPC_NONE) {
    ARITH_DOUBLE_SCALE_SETTLE;
  }
#endif


  // frem
	// compile: flush_cache, frem, fst
  CODE(opc_frem, frem, ST0, ST0, OPC_NONE) {
    asm("flds  (%esp)\n\t"		// fld optop[-1].f (value2)
	"flds  4(%esp)");		// fld optop[-2].f (value1)
    FUNCCALL_IN(0);
    asm("subl  $16,%esp");
    asm("fstpl (%esp)\n\t"		// stack top   = optop[-2] (value1)
	"fstpl 8(%esp)\n\t"		// stack top-1 = optop[-1] (value2)
	"call  " FUNCTION(fmod) "\n\t"
	"addl  $16,%esp");
    FUNCCALL_OUT(0);
    asm("addl  $4,%esp");
	// now state 0
#ifdef USE_SSE2
    asm("fstps (%esp)");	// opc_fst (!USE_SSE2)
#endif
  }


  // drem
	// compile: fill_cache, drem, dst
#define DREM_ST24(OPTOP1_REG, OPTOP2_REG) \
    asm("popl  %eax\n\t"	/* eax = v1[0:31] */\
	"popl  %edi");		/* edi = v1[32:63] */\
    FUNCCALL_IN(0);\
    asm("pushl " #OPTOP2_REG "\n\t"	/* push v2[32:63] */\
	"pushl " #OPTOP1_REG "\n\t"	/* push v2[0:31] */\
	"pushl %edi\n\t"	/* push v1[32:63] */\
	"pushl %eax\n\t"	/* push v1[0:31] */\
	"call  " FUNCTION(fmod) "\n\t"\
	"addl  $16,%esp");\
    FUNCCALL_OUT(0);\
    asm("subl  $8,%esp");
	// now state 0

  CODE(opc_drem, drem, ST2, ST0, OPC_NONE) {
    DREM_ST24(%ecx, %edx);
#ifdef USE_SSE2
    asm("fstpl (%esp)");	// opc_dst (!USE_SSE2)
#endif
  }
  CODE(opc_drem, drem, ST4, ST0, OPC_NONE) {
    DREM_ST24(%edx, %ecx);
#ifdef USE_SSE2
    asm("fstpl (%esp)");	// opc_dst (!USE_SSE2)
#endif
  }


  // ineg
  CODE(opc_ineg, ineg, ST0, ST1, OPC_NONE) {
    asm("popl  %edx\n\t"	// now state 1
	"negl  %edx");
    VALUE_DEBUG(%edx);
  }
  CODE(opc_ineg, ineg, ST1, ST1, OPC_NONE) {
    asm("negl  %edx");
    VALUE_DEBUG(%edx);
  }
  CODE(opc_ineg, ineg, ST2, ST2, OPC_NONE) {
    asm("negl  %ecx");
    VALUE_DEBUG(%ecx);
  }
  CODE(opc_ineg, ineg, ST3, ST3, OPC_NONE) {
    asm("negl  %ecx");
    VALUE_DEBUG(%ecx);
  }
  CODE(opc_ineg, ineg, ST4, ST4, OPC_NONE) {
    asm("negl  %edx");
    VALUE_DEBUG(%edx);
  }


  // lneg
#define LNEG_ST24(OPTOP1_REG, OPTOP2_REG) \
    asm("negl  " #OPTOP1_REG "\n\t"\
	"adcl  $0," #OPTOP2_REG "\n\t"\
	"negl  " #OPTOP2_REG);\
    VALUE64_DEBUG(OPTOP1_REG, OPTOP2_REG)
	// now state [24]

  CODE(opc_lneg, lneg, ST0, ST2, OPC_NONE) {
    asm("popl  %ecx\n\t"
	"popl  %edx");	// now state 2
    LNEG_ST24(%ecx, %edx);
  }
  CODE(opc_lneg, lneg, ST1, ST4, OPC_NONE) {
    asm("popl  %ecx");	// now state 4
    LNEG_ST24(%edx, %ecx);
  }
  CODE(opc_lneg, lneg, ST2, ST2, OPC_NONE) {
    LNEG_ST24(%ecx, %edx);
  }
  CODE(opc_lneg, lneg, ST3, ST2, OPC_NONE) {
    asm("popl  %edx");	// now state 2
    LNEG_ST24(%ecx, %edx);
  }
  CODE(opc_lneg, lneg, ST4, ST4, OPC_NONE) {
    LNEG_ST24(%edx, %ecx);
  }


  // fneg
#define CODE_FNEG(VOP, STATE, TGT) \
  CODE(opc_##VOP, VOP, STATE, STSTA, OPC_NONE) {\
    asm("xorl  $0x80000000," STR(TGT));\
  }

  CODE_FNEG(fneg, ST0, (%esp));
  CODE_FNEG(fneg, ST1, %edx);
  CODE_FNEG(fneg, ST2, %ecx);
  CODE_FNEG(fneg, ST3, %ecx);
  CODE_FNEG(fneg, ST4, %edx);


  // dneg
  CODE_FNEG(dneg, ST0, 4(%esp));
  CODE_FNEG(dneg, ST1, (%esp));
  CODE_FNEG(dneg, ST2, %edx);
  CODE_FNEG(dneg, ST3, (%esp));
  CODE_FNEG(dneg, ST4, %ecx);


  // ishl
#define SHIFT_INT_ST2(ROP) \
    asm(#ROP "l  %cl,%edx")
	// now state 1

#define CODE_SHIFT_INT(VOP, ROP) \
  CODE(opc_i##VOP, i##VOP, ST0, ST1, OPC_NONE) {\
    asm("popl  %ecx\n\t"\
	"popl  %edx");	/* now state 2 */\
    SHIFT_INT_ST2(ROP);\
    VALUE_DEBUG(%edx);\
  }\
  CODE(opc_i##VOP, i##VOP, ST1, ST1, OPC_NONE) {\
    asm("movl  %edx,%ecx\n\t"\
	"popl  %edx\n\t");	/* now state 2 */\
    SHIFT_INT_ST2(ROP);\
    VALUE_DEBUG(%edx);\
  }\
  CODE(opc_i##VOP, i##VOP, ST2, ST1, OPC_NONE) {\
    SHIFT_INT_ST2(ROP);\
    VALUE_DEBUG(%edx);\
  }\
  CODE(opc_i##VOP, i##VOP, ST3, ST1, OPC_NONE) {\
    asm("popl  %edx");	/* now state 2 */\
    SHIFT_INT_ST2(ROP);\
    VALUE_DEBUG(%edx);\
  }\
  CODE(opc_i##VOP, i##VOP, ST4, ST1, OPC_NONE) {\
    asm("xchg  %ecx,%edx");	/* now state 2 */\
    SHIFT_INT_ST2(ROP);\
    VALUE_DEBUG(%edx);\
  }

  CODE_SHIFT_INT(shl, shl);
  CODE_SHIFT_INT(shr, sar);
  CODE_SHIFT_INT(ushr, shr);


  // lshl, lshr, lushr
	// %cl is shift count.
	// OP2 and OP3 are %eax and %edx.
#if 1	// code generated by egcs-1.0.3
#define SHIFT_SIGNED_LONG(ROP64, ROP32, OP2_REG, OP3_REG, LABEL) \
    asm(#ROP64 "l %cl," #OP2_REG "," #OP3_REG "\n\t"\
	#ROP32 "l %cl," #OP2_REG "\n\t"\
	"testb $0x20,%cl\n\t"\
	"jz    " LABEL "\n\t"\
	"movl  " #OP2_REG "," #OP3_REG "\n\t"\
	#ROP32 "l $0x1f," #OP2_REG "\n\t"\
      LABEL ":\n\t"\
	"movl  %eax,%ecx")
	// now state 2 or 4
#define SHIFT_UNSIGNED_LONG(ROP64, ROP32, OP2_REG, OP3_REG, LABEL) \
    asm(#ROP64 "l %cl," #OP2_REG "," #OP3_REG "\n\t"\
	#ROP32 "l %cl," #OP2_REG "\n\t"\
	"testb $0x20,%cl\n\t"\
	"jz    " LABEL "\n\t"\
	"movl  " #OP2_REG "," #OP3_REG "\n\t"\
	"xorl  " #OP2_REG "," #OP2_REG "\n\t"	/* clear OP2_REG */\
      LABEL ":\n\t"\
	"movl  %eax,%ecx")
	// now state 2 or 4
#else	// code generated by gcc-2.7.2.3
#define SHIFT_LONG(ROP64, ROP32, OP2_REG, OP3_REG, LABEL) \
    asm("rorb  %cl\n\t"\
	#ROP64 "l %cl," #OP2_REG "," #OP3_REG "\n\t"\
	#ROP32 "l  %cl," #OP2_REG "\n\t"\
	#ROP64 "l %cl," #OP2_REG "," #OP3_REG "\n\t"\
	#ROP32 "l  %cl," #OP2_REG "\n\t"\
	"shrb  $7,%cl\n\t"\
	#ROP64 "l %cl," #OP2_REG "," #OP3_REG "\n\t"\
	#ROP32 "l  %cl," #OP2_REG "\n\t"\
	"movl  %eax,%ecx")
	// now state 2 or 4
#endif

#define CODE_SHIFT_LONG(SIGNEDP, VOP, ROP64, ROP32, REG_A, REG_B) \
  CODE(opc_l##VOP, l##VOP, ST0, ST2, OPC_NONE) {\
    asm("popl  %ecx\n\t"	/* ecx = shift count */\
	"popl  %eax\n\t"	/* eax = [0:31] */\
	"popl  %edx");		/* edx = [32:63] */\
    SHIFT_##SIGNEDP##_LONG(ROP64, ROP32, REG_A, REG_B, #VOP "_st0");\
    VALUE64_DEBUG(%ecx, %edx);\
  }\
  CODE(opc_l##VOP, l##VOP, ST1, ST2, OPC_NONE) {\
    asm("movl  %edx,%ecx\n\t"	/* ecx = shift count (%edx) */\
	"popl  %eax\n\t"	/* eax = [0:31] */\
	"popl  %edx");		/* edx = [32:63] */\
    SHIFT_##SIGNEDP##_LONG(ROP64, ROP32, REG_A, REG_B, #VOP "_st1");\
    VALUE64_DEBUG(%ecx, %edx);\
  }\
  CODE(opc_l##VOP, l##VOP, ST2, ST4, OPC_NONE) {\
    asm("popl  %eax");		/* eax = [32:63] */\
    SHIFT_##SIGNEDP##_LONG(ROP64, ROP32, REG_B, REG_A, #VOP "_st2");\
    VALUE64_DEBUG(%edx, %ecx);\
  }\
  CODE(opc_l##VOP, l##VOP, ST3, ST2, OPC_NONE) {\
    asm("popl  %eax\n\t"	/* eax = [0:31] */\
	"popl  %edx");		/* edx = [32:63] */\
    SHIFT_##SIGNEDP##_LONG(ROP64, ROP32, REG_A, REG_B, #VOP "_st3");\
    VALUE64_DEBUG(%ecx, %edx);\
  }\
  CODE(opc_l##VOP, l##VOP, ST4, ST2, OPC_NONE) {\
    asm("movl  %ecx,%eax\n\t"	/* eax = [0:31] (ecx) */\
	"movl  %edx,%ecx\n\t"	/* ecx = shift count (edx) */\
	"popl  %edx");		/* edx = [32:63] */\
    SHIFT_##SIGNEDP##_LONG(ROP64, ROP32, REG_A, REG_B, #VOP "_st4");\
    VALUE64_DEBUG(%ecx, %edx);\
  }

  CODE_SHIFT_LONG(UNSIGNED, shl, shld, shl, %eax, %edx);
  CODE_SHIFT_LONG(SIGNED, shr, shrd, sar, %edx, %eax);
  CODE_SHIFT_LONG(UNSIGNED, ushr, shrd, shr, %edx, %eax);


  // iinc
	// const: (signed char *pc)[2], pc[1] * 4
#ifdef RUNTIME_DEBUG
#  define IINC_DEBUG1 \
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl " STR(SLOT_CONST) "(%esi)\n\t"\
	"pushl $" STR(SLOT_CONST));\
    PUSH_CONSTSTR("  var[%d] %d");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $12,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }

#  define IINC_DEBUG2 \
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl " STR(SLOT_CONST) "(%esi)\n\t"\
	"pushl $" STR(SLOT_CONST));\
    PUSH_CONSTSTR(" + %d = %d\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $12,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }
#else
#  define IINC_DEBUG1
#  define IINC_DEBUG2
#endif

  CODE(opc_iinc, iinc, STANY, STSTA, OPC_NONE) {
    IINC_DEBUG1;
    asm("addl  $" STR(SLOT_CONST) "," STR(SLOT_CONST) "(%esi)");
    IINC_DEBUG2;
  }


  // i2l
  CODE(opc_i2l, i2l, ST0, ST2, OPC_NONE) {
    asm("popl  %edx\n\t"	// now state 1
	"movl  %edx,%ecx\n\t"
	"sarl  $31,%edx");
  }
  CODE(opc_i2l, i2l, ST1, ST2, OPC_NONE) {
    asm("movl  %edx,%ecx\n\t"
	"sarl  $31,%edx");
  }
  CODE(opc_i2l, i2l, ST2, ST4, OPC_NONE) {
    asm("pushl %edx\n\t"	// now state 3
	"movl  %ecx,%edx\n\t"
	"sarl  $31,%ecx");
  }
  CODE(opc_i2l, i2l, ST3, ST4, OPC_NONE) {
    asm("movl  %ecx,%edx\n\t"
	"sarl  $31,%ecx");
  }
  CODE(opc_i2l, i2l, ST4, ST2, OPC_NONE) {
    asm("pushl %ecx\n\t"	// now state 1
	"movl  %edx,%ecx\n\t"
	"sarl  $31,%edx");
  }


  // i2f
	// compile: flush_cache, i2f, fst
  CODE(opc_i2f, i2f, ST0, ST0, OPC_NONE) {
#ifdef USE_SSE2
    asm("cvtsi2ss (%esp),%xmm0");
#else
    asm("fildl (%esp)");
#endif
  }


  // i2d
	// compile: flush_cache, i2d, dst
  CODE(opc_i2d, i2d, ST0, ST0, OPC_NONE) {
#ifdef USE_SSE2
    asm("cvtsi2sd (%esp),%xmm0");
#else
    asm("fildl (%esp)");
#endif
    asm("subl  $4,%esp");
  }


  // l2i
  CODE(opc_l2i, l2i, ST0, ST1, OPC_NONE) {
    asm("popl  %edx\n\t"	// now state 1
	"addl  $4,%esp");
  }
  CODE(opc_l2i, l2i, ST1, ST1, OPC_NONE) {
    asm("addl  $4,%esp");
  }
  CODE(opc_l2i, l2i, ST2, ST3, OPC_NONE) {}
  CODE(opc_l2i, l2i, ST3, ST3, OPC_NONE) {
    asm("addl  $4,%esp");
  }
  CODE(opc_l2i, l2i, ST4, ST1, OPC_NONE) {}


  // l2f
	// compile: flush_cache, l2f, (fst if USE_SSE2 is not defined)
  CODE(opc_l2f, l2f, ST0, ST0, OPC_NONE) {
    asm("fildll (%esp)\n\t"
	"addl  $4,%esp");
#ifdef USE_SSE2
    asm("fstps (%esp)");	// opc_fst (!USE_SSE2)
#endif
  }


  // l2d
	// compile: flush_cache, l2d, (dst if USE_SSE2 is not defined)
  CODE(opc_l2d, l2d, ST0, ST0, OPC_NONE) {
    asm("fildll (%esp)");
#ifdef USE_SSE2
    asm("fstpl (%esp)");	// opc_dst (!USE_SSE2)
#endif
  }


  // f2i, f2l, d2i, d2l
#ifdef RUNTIME_DEBUG
#  define REAL2INT_DEBUG1 \
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("subl  $8,%esp\n\t"\
	"fstl  (%esp)");\
    PUSH_CONSTSTR("  %g\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $12,%esp");\
    DEBUG_OUT;\
  }
#  define REAL2INT_DEBUG2 \
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl $0\n"\
	"fstcw (%esp)");\
    PUSH_CONSTSTR("  FPU cw: 0x%x\n");\
    asm("call " FUNCTION(printf) "\n\t"\
	"addl  $8,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }
#else
#  define REAL2INT_DEBUG1
#  define REAL2INT_DEBUG2
#endif

#define REAL2INT_ST0(FLD, ADJ, FIST) \
    asm(#FLD "  (%esp)");\
    REAL2INT_DEBUG1;\
    asm("subl  $(4+(" #ADJ ")),%esp\n\t"\
	"fstcw (%esp)\n\t"	/* original control word */\
	"ftst\n\t"\
	"movw  (%esp),%ax\n\t"\
	"orw   $(3<<10),%ax\n\t"	/* rounding mode */\
	"movw  %ax,2(%esp)\n\t"\
	"fldcw 2(%esp)\n\t"\
	#FIST " 4(%esp)\n\t"\
	"fldcw (%esp)\n\t"\
	"addl  $4,%esp");\
    REAL2INT_DEBUG2;
	// now state 0

#define REAL2INT_F2I_ST0	REAL2INT_ST0(flds, 0, fistpl)
#define REAL2INT_F2L_ST0	REAL2INT_ST0(flds, 4, fistpll)
#define REAL2INT_D2I_ST0	REAL2INT_ST0(fldl, -4, fistpl)
#define REAL2INT_D2L_ST0	REAL2INT_ST0(fldl, 0, fistpll)

#define REAL2INT_CHECK_INT(VOP, STATE) \
    asm("popl  %edx\n\t"	/* now state 1 */\
	"cmpl  $0x80000000,%edx\n\t"\
	"jne   " #VOP "_st" #STATE "_done\n\t"\
	"fnstsw %ax\n\t"\
	"sahf\n\t"\
	"jp    " #VOP "_st" #STATE "_nan\n\t"	/* jump if not NaN */\
	"adcl  $-1,%edx\n\t"	/* carry flag is set if result < 0 */\
	"jmp   " #VOP "_st" #STATE "_done\n\t"\
      #VOP "_st" #STATE "_nan:\n\t"	/* result is NaN: return 0 */\
	"xorl  %edx,%edx\n\t"\
      #VOP "_st" #STATE "_done:")
	// now state 1

#define REAL2INT_CHECK_LONG(VOP, STATE) \
    asm("popl  %ecx\n\t"	/* ecx = low word */\
	"popl  %edx\n\t"	/* edx = high word */\
		/* now state 2 */\
	/* check if high word is 0x80000000 and low word is 0 */\
	"movl  %edx,%eax\n\t"\
	"xorl  $0x80000000,%eax\n\t"\
	"orl   %ecx,%eax\n\t"\
	"jnz   " #VOP "_st" #STATE "_done\n\t"\
	"fnstsw %ax\n\t"\
	"sahf\n\t"\
	"jp    " #VOP "_st" #STATE "_nan\n\t"	/* jump if not NaN */\
	"adcl  $-1,%ecx\n\t"	/* carry flag is set if result < 0 */\
	"adcl  $-1,%edx\n\t"\
	"jmp   " #VOP "_st" #STATE "_done\n\t"\
      #VOP "_st" #STATE "_nan:\n\t"	/* result is NaN: return 0 */\
	"xorl  %edx,%edx\n\t"\
	"xorl  %ecx,%ecx\n\t"\
      #VOP "_st" #STATE "_done:")
	// now state 2

#define CODE_REAL2INT(vop, VOP, RET_TYPE, LAST_STATE) \
  CODE(opc_##vop, vop, ST0, ST##LAST_STATE, OPC_NONE) {\
    REAL2INT_##VOP##_ST0;\
    REAL2INT_CHECK_##RET_TYPE(vop, 0);\
  }\
  CODE(opc_##vop, vop, ST1, ST##LAST_STATE, OPC_NONE) {\
    asm("pushl %edx");	/* now state 0 */\
    REAL2INT_##VOP##_ST0;\
    REAL2INT_CHECK_##RET_TYPE(vop, 1);\
  }\
  CODE(opc_##vop, vop, ST2, ST##LAST_STATE, OPC_NONE) {\
    asm("pushl %edx\n\t"\
	"pushl %ecx");	/* now state 0 */\
    REAL2INT_##VOP##_ST0;\
    REAL2INT_CHECK_##RET_TYPE(vop, 2);\
  }\
  CODE(opc_##vop, vop, ST3, ST##LAST_STATE, OPC_NONE) {\
    asm("pushl %ecx");	/* now state 0 */\
    REAL2INT_##VOP##_ST0;\
    REAL2INT_CHECK_##RET_TYPE(vop, 3);\
  }\
  CODE(opc_##vop, vop, ST4, ST##LAST_STATE, OPC_NONE) {\
    asm("pushl %ecx\n\t"\
	"pushl %edx");	/* now state 0 */\
    REAL2INT_##VOP##_ST0;\
    REAL2INT_CHECK_##RET_TYPE(vop, 4);\
  }

  CODE_REAL2INT(f2i, F2I, INT, 1);
  CODE_REAL2INT(f2l, F2L, LONG, 2);
  CODE_REAL2INT(d2i, D2I, INT, 1);
  CODE_REAL2INT(d2l, D2L, LONG, 2);


  // f2d
	// compile: flush_cache, fld, f2d, dst
  CODE(opc_f2d, f2d, ST0, ST0, OPC_NONE) {
#ifdef USE_SSE2
    asm("cvtss2sd %xmm0,%xmm0");
#endif
    asm("subl  $4,%esp");
  }
#ifdef OPTIMIZE_INTERNAL_CODE
  CODE(opc_f2d, f2d, ST1, ST0, OPC_NONE) {
#  ifdef USE_SSE2
    asm("cvtss2sd %xmm0,%xmm0");
#  endif
    asm("subl  $8,%esp");
  }
#endif


  // d2f
	// compile: flush_cache, dld, d2f, fst
  CODE(opc_d2f, d2f, ST0, ST0, OPC_NONE) {
#ifdef USE_SSE2
    asm("cvtsd2ss %xmm0,%xmm0");
#endif
    asm("addl  $4,%esp");
  }
#ifdef OPTIMIZE_INTERNAL_CODE
  CODE(opc_d2f, d2f, ST2, ST0, OPC_NONE) {
#  ifdef USE_SSE2
    asm("cvtsd2ss %xmm0,%xmm0");
#  endif
    asm("subl  $4,%esp");
  }
#endif


  // i2b, i2c, i2s
#define I2B(REG) \
    asm("shl  $24," #REG "\n\t"\
	"sar  $24," #REG)
#define I2C(REG) \
    asm("shl  $16," #REG "\n\t"\
	"shr  $16," #REG)
#define I2S(REG) \
    asm("shl  $16," #REG "\n\t"\
	"sar  $16," #REG)

#define CODE_I2BCS(vop, VOP) \
  CODE(opc_##vop, vop, ST0, ST1, OPC_NONE) {\
    asm("popl  %edx");	/* now state 1 */\
    VOP(%edx);\
  }\
  CODE(opc_##vop, vop, ST1, ST1, OPC_NONE) {\
    VOP(%edx);\
  }\
  CODE(opc_##vop, vop, ST2, ST2, OPC_NONE) {\
    VOP(%ecx);\
  }\
  CODE(opc_##vop, vop, ST3, ST3, OPC_NONE) {\
    VOP(%ecx);\
  }\
  CODE(opc_##vop, vop, ST4, ST4, OPC_NONE) {\
    VOP(%edx);\
  }

  CODE_I2BCS(i2b, I2B);
  CODE_I2BCS(i2c, I2C);
  CODE_I2BCS(i2s, I2S);


  // lcmp
#define LCMP_ST24(OPTOP1_REG, OPTOP2_REG, STATE) \
    asm("popl  %eax\n\t"	/* eax = v1[0:31] */\
	"popl  %edi");		/* edi = v1[32:63] */\
    ARITH_LONG_DEBUG1(%eax, %edi, OPTOP1_REG, OPTOP2_REG, "");\
    asm("cmpl  " #OPTOP2_REG ",%edi\n\t"	/* cmp v1[32:63] - v2[32:63]*/\
	"jl    lcmp_st" #STATE "_lt\n\t"\
	"jg    lcmp_st" #STATE "_ge\n\t"\
	"cmpl  " #OPTOP1_REG ",%eax\n\t"	/* cmp v1[0:31] - v2[0:31] */\
	"jb    lcmp_st" #STATE "_lt\n\t"\
      "lcmp_st" #STATE "_ge:\n\t"\
	"movl  $0,%edx\n\t"\
	"setnz %dl\n\t"\
	"jmp   lcmp_st" #STATE "_done\n\t"\
      "lcmp_st" #STATE "_lt:\n\t"\
	"movl  $-1,%edx\n\t"\
      "lcmp_st" #STATE "_done:")
	// now state 1

  CODE(opc_lcmp, lcmp, ST0, ST1, OPC_NONE) {
    asm("popl  %ecx\n\t"
	"popl  %edx");	// now state 2
    LCMP_ST24(%ecx, %edx, 0);
  }
  CODE(opc_lcmp, lcmp, ST1, ST1, OPC_NONE) {
    asm("popl  %ecx");	// now state 4
    LCMP_ST24(%edx, %ecx, 1);
  }
  CODE(opc_lcmp, lcmp, ST2, ST1, OPC_NONE) {
    LCMP_ST24(%ecx, %edx, 2);
  }
  CODE(opc_lcmp, lcmp, ST3, ST1, OPC_NONE) {
    asm("popl  %edx");	// now state 2
    LCMP_ST24(%ecx, %edx, 3);
  }
  CODE(opc_lcmp, lcmp, ST4, ST1, OPC_NONE) {
    LCMP_ST24(%edx, %ecx, 4);
  }


  // fcmpl, fcmpg, dcmpl, dcmpg
#ifdef RUNTIME_DEBUG
#  define FCMP_DEBUG1 \
  if (runtime_debug) {\
    asm("flds  4(%esp)\n\t"\
	"flds  (%esp)");\
    DEBUG_IN;\
    asm("subl  $16,%esp\n\t"\
	"fstpl 8(%esp)\n\t"\
	"fstpl (%esp)");\
    PUSH_CONSTSTR("  %g %g\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $20,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }
#  define DCMP_DEBUG1 \
  if (runtime_debug) {\
    asm("movl  (%esp),%edi\n\t"\
	"movl  4(%esp),%eax\n\t"\
	"movl  8(%esp),%ecx\n\t"\
	"movl  12(%esp),%edx");\
    DEBUG_IN;\
    asm("pushl %eax\n\tpushl %edi\n\t"\
	"pushl %edx\n\tpushl %ecx");\
    PUSH_CONSTSTR("  %g %g\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $20,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }
#else
#  define FCMP_DEBUG1
#  define DCMP_DEBUG1
#endif

#define FCMPL_COMPARE_ST0 \
    FCMP_DEBUG1;\
    asm("flds  4(%esp)\n\t"\
	"fcomps (%esp)\n\t"\
	"addl  $8,%esp")
#define FCMPG_COMPARE_ST0	FCMPL_COMPARE_ST0

#define DCMPL_COMPARE_ST0 \
    DCMP_DEBUG1;\
    asm("fldl  8(%esp)\n\t"\
	"fcompl (%esp)\n\t"\
	"addl  $16,%esp")
#define DCMPG_COMPARE_ST0	DCMPL_COMPARE_ST0

#define FLOAT_CMP_NAN_g(LABEL) \
	"jnp  " LABEL "_normal\n\t"	/* PF indicates NaN */\
	"movl  $1,%edx\n\t"\
	"jmp  " LABEL "_done\n\t"
#define FLOAT_CMP_NAN_l(LABEL) \
	"jp   " LABEL "_l\n\t"		/* PF indicates NaN */

#define FLOAT_CMP_ST0(SUF, LABEL) \
    asm("fnstsw %ax\n\t"\
	"sahf\n\t"\
	FLOAT_CMP_NAN_##SUF(LABEL)\
      LABEL "_normal:\n\t"\
	"jc    " LABEL "_l\n\t"\
      LABEL "_g:\n\t"\
	"movl  $0,%edx\n\t"\
	"setnz %dl\n\t"\
	"jmp   " LABEL "_done\n\t"\
      LABEL "_l:\n\t"\
	"movl  $-1,%edx\n\t"\
      LABEL "_done:")
	// now state 1

#define CODE_FLOAT_CMP(vop, VOP, SUF) \
  CODE(opc_##vop, vop, ST0, ST1, OPC_NONE) {\
    VOP##_COMPARE_ST0;\
    FLOAT_CMP_ST0(SUF, #VOP "_st0");\
  }\
  CODE(opc_##vop, vop, ST1, ST1, OPC_NONE) {\
    asm("pushl %edx");	/* now state 0 */\
    VOP##_COMPARE_ST0;\
    FLOAT_CMP_ST0(SUF, #VOP "_st1");\
  }\
  CODE(opc_##vop, vop, ST2, ST1, OPC_NONE) {\
    asm("pushl %edx\n\t"\
	"pushl %ecx");	/* now state 0 */\
    VOP##_COMPARE_ST0;\
    FLOAT_CMP_ST0(SUF, #VOP "_st2");\
  }\
  CODE(opc_##vop, vop, ST3, ST1, OPC_NONE) {\
    asm("pushl %ecx");	/* now state 0 */\
    VOP##_COMPARE_ST0;\
    FLOAT_CMP_ST0(SUF, #VOP "_st3");\
  }\
  CODE(opc_##vop, vop, ST4, ST1, OPC_NONE) {\
    asm("pushl %ecx\n\t"\
	"pushl %edx");	/* now state 0 */\
    VOP##_COMPARE_ST0;\
    FLOAT_CMP_ST0(SUF, #VOP "_st4");\
  }

  CODE_FLOAT_CMP(fcmpl, FCMPL, l);
  CODE_FLOAT_CMP(fcmpg, FCMPG, g);
  CODE_FLOAT_CMP(dcmpl, DCMPL, l);
  CODE_FLOAT_CMP(dcmpg, DCMPG, g);


  // ifeq, ifne, iflt, ifge, ifgt, ifle
#define IF(OPTOP1_REG, JP_ROP) \
    VALUE_DEBUG(OPTOP1_REG);\
    asm("testl " #OPTOP1_REG "," #OPTOP1_REG)
    // jump: #JP_ROP STR(SLOT_ADDR_JP)

#define CODE_IF(VOP, JP_ROP) \
  CODE(opc_if##VOP, if##VOP, ST0, ST0, OPC_JUMP) {\
    asm("popl  %eax");\
    IF(%eax, JP_ROP);\
  }\
  CODE(opc_if##VOP, if##VOP, ST1, ST0, OPC_NONE) {\
    IF(%edx, JP_ROP);\
  }\
  CODE(opc_if##VOP, if##VOP, ST2, ST1, OPC_NONE) {\
    IF(%ecx, JP_ROP);\
  }\
  CODE(opc_if##VOP, if##VOP, ST3, ST0, OPC_NONE) {\
    IF(%ecx, JP_ROP);\
  }\
  CODE(opc_if##VOP, if##VOP, ST4, ST3, OPC_NONE) {\
    IF(%edx, JP_ROP);\
  }

  CODE_IF(eq, je);
  CODE_IF(ne, jne);
  CODE_IF(lt, jl);
  CODE_IF(ge, jge);
  CODE_IF(gt, jg);
  CODE_IF(le, jle);


  // if_icmp{eq,ne,lt,ge,gt,le}
#ifdef RUNTIME_DEBUG
#  define IF_ICMP_DEBUG(OPTOP1_REG, OPTOP2_REG) \
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl " #OPTOP1_REG "\n\t"\
	"pushl " #OPTOP2_REG);\
    PUSH_CONSTSTR("  %d %d\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $12,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }
#else
#  define IF_ICMP_DEBUG(OPTOP1_REG, OPTOP2_REG)
#endif

#define IF_ICMP_ST24(OPTOP1_REG, OPTOP2_REG, JP_ROP) \
    IF_ICMP_DEBUG(OPTOP1_REG, OPTOP2_REG);\
    asm("cmpl  " #OPTOP1_REG "," #OPTOP2_REG)
    // jump: #JP_ROP STR(SLOT_ADDR_JP)
	// now state 0

#define CODE_IF_ICMP(VOP, JP_ROP) \
  CODE(opc_if_icmp##VOP, if_icmp##VOP, ST0, ST0, OPC_JUMP) {\
    asm("popl  %ecx\n\t"\
	"popl  %edx");	/* now state 2 */\
    IF_ICMP_ST24(%ecx, %edx, JP_ROP);\
  }\
  CODE(opc_if_icmp##VOP, if_icmp##VOP, ST1, ST0, OPC_NONE) {\
    asm("popl  %ecx");	/* now state 4 */\
    IF_ICMP_ST24(%edx, %ecx, JP_ROP);\
  }\
  CODE(opc_if_icmp##VOP, if_icmp##VOP, ST2, ST0, OPC_NONE) {\
    IF_ICMP_ST24(%ecx, %edx, JP_ROP);\
  }\
  CODE(opc_if_icmp##VOP, if_icmp##VOP, ST3, ST0, OPC_NONE) {\
    asm("popl  %edx");	/* now state 2 */\
    IF_ICMP_ST24(%ecx, %edx, JP_ROP);\
  }\
  CODE(opc_if_icmp##VOP, if_icmp##VOP, ST4, ST0, OPC_NONE) {\
    IF_ICMP_ST24(%edx, %ecx, JP_ROP);\
  }

  CODE_IF_ICMP(eq, je);
  CODE_IF_ICMP(ne, jne);
  CODE_IF_ICMP(lt, jl);
  CODE_IF_ICMP(ge, jge);
  CODE_IF_ICMP(gt, jg);
  CODE_IF_ICMP(le, jle);


  // goto
  CODE(opc_goto, goto, STANY, STSTA, OPC_JUMP) {
    // jump: "jmp   " STR(SLOT_ADDR_JP)
  }


  // jsr
	// const: native offset of a next instruction
#ifdef RUNTIME_DEBUG
#  define JSR_DEBUG1(REG) \
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl " #REG);\
    PUSH_CONSTSTR("  push 0x%08x\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $8,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }
#else
#  define JSR_DEBUG1(REG)
#endif

#define JSR(REG) \
    asm("movl  $" STR(SLOT_CONST) "," #REG);\
    JSR_DEBUG1(REG)
    // jump: "jmp  " STR(SLOT_ADDR_JP)

  CODE(opc_jsr, jsr, ST0, ST1, OPC_JUMP) {
    JSR(%edx);
  }
  CODE(opc_jsr, jsr, ST1, ST2, OPC_JUMP) {
    JSR(%ecx);
  }
  CODE(opc_jsr, jsr, ST2, ST4, OPC_JUMP) {
    asm("pushl %edx");	// now state 3
    JSR(%edx);
  }
  CODE(opc_jsr, jsr, ST3, ST4, OPC_JUMP) {
    JSR(%edx);
  }
  CODE(opc_jsr, jsr, ST4, ST2, OPC_JUMP) {
    asm("pushl %ecx");	// now state 1
    JSR(%ecx);
  }


  // ret
	// const: index * 4
  CODE(opc_ret, ret, STANY, STSTA, OPC_JUMP) {
    asm("movl " STR(SLOT_CONST) "(%esi),%eax");	// eax = vars[index]
    ILOAD_DEBUG1(%eax);
    COMPILEDCODE(%edi);		// edi = mb->CompiledCode
    asm("addl  %edi,%eax");
    // jump: "jmp   *%eax"
  }


  // tableswitch
	// const: low, high, table offset (in native)
  //
  // table:  Each element is 8 byte.
  //  An element consists of an offset of target in native code
  //  and a pointer to trampoline code.
  //
  //	default, offset(low), offset(low+1), ..., offset(high)
#ifdef RUNTIME_DEBUG
#  define TBLSW_DEBUG1(INDEX) \
  asm("pushl %eax");\
  if (runtime_debug) {		/* break eax */ \
    asm("movl  (%esp),%eax");	/* restore */ \
    DEBUG_IN;\
    asm("pushl %eax\n\tpushl %edi\n\tpushl " #INDEX);\
    PUSH_CONSTSTR("  index: %d [%d:%d]\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $16,%esp");\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#  define TBLSW_DEBUG2(OFF) \
  asm("pushl %eax");\
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl " #OFF "\n\tpushl " #OFF);\
    PUSH_CONSTSTR("  native off: 0x%08x(%d)\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $12,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#else
#  define TBLSW_DEBUG1(INDEX)
#  define TBLSW_DEBUG2(OFF)
#endif

#define TBLSW(INDEX, LABEL) \
    asm("movl  $" STR(SLOT_CONST) ",%edi\n\t"	/* low */\
	"movl  $" STR(SLOT_CONST) ",%eax");		/* high */\
    TBLSW_DEBUG1(INDEX);\
    asm("subl  %edi," #INDEX "\n\t"		/* index -= low */\
	"subl  %edi,%eax");			/* high -= low */\
    \
    COMPILEDCODE(%edi);				/* edi = mb->CompiledCode */\
    asm("pushl %edi");	/* push mb->CompiledCode */\
    \
    asm("addl  $" STR(SLOT_CONST) ",%edi"); /* edi += offset of the table */\
		/* edi = addr. of the table */\
    asm("cmpl  " #INDEX ",%eax\n\t"		/* test high - index */\
	"jb    " LABEL "_default\n\t"\
	"leal  8(%edi," #INDEX ",8),%edi\n\t"	/* edi = tgt offset */\
      LABEL "_default:\n\t"\
	"movl  (%edi),%eax");\
    TBLSW_DEBUG2(%eax);\
    \
    asm("addl  (%esp),%eax\n\t"	/* eax += mb->CompiledCode */\
	"addl  $4,%esp");\
		/* eax = target address */\
    \
    asm("jmp   *4(%edi)")

  CODE(opc_tableswitch, tableswitch, ST0, ST0, OPC_JUMP) {
    asm("popl  %edx");	// now state 1
    TBLSW(%edx, "tblsw_st0");
  }
  CODE(opc_tableswitch, tableswitch, ST1, ST0, OPC_JUMP) {
    TBLSW(%edx, "tblsw_st1");
  }
  CODE(opc_tableswitch, tableswitch, ST2, ST1, OPC_JUMP) {
    TBLSW(%ecx, "tblsw_st2");
  }
  CODE(opc_tableswitch, tableswitch, ST3, ST0, OPC_JUMP) {
    TBLSW(%ecx, "tblsw_st3");
  }
  CODE(opc_tableswitch, tableswitch, ST4, ST3, OPC_JUMP) {
    TBLSW(%edx, "tblsw_st4");
  }


  // lookupswitch
	// const: npairs, table offset (in native)
  // table:  Each element is 12 byte.
  //  An element consists of a key, an offset of target in native code
  //  and a pointer to trampoline code.
  //
  //	element(1), element(2), ..., element(npairs), element(default)
#ifdef RUNTIME_DEBUG
#  define LUSW_DEBUG1(KEY) \
  asm("pushl %eax");\
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl %eax\n\tpushl " #KEY);\
    PUSH_CONSTSTR("  key: %d, npairs: %d\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $12,%esp");\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#  define LUSW_DEBUG2 \
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl 8(%edi)\n\t"\
	"pushl 4(%edi)\n\tpushl 4(%edi)\n\t"\
	"pushl (%edi)");\
    PUSH_CONSTSTR("  match: %d, target offset: 0x%08x(%d), trampoline code: 0x%08x\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $20,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }
#else
#  define LUSW_DEBUG1(KEY)
#  define LUSW_DEBUG2
#endif

#define LUSW(KEY, LABEL) \
    COMPILEDCODE(%edi);\
    asm("pushl %edi");	/* push mb->CompiledCode */\
    \
    asm("movl  $" STR(SLOT_CONST) ",%eax");	/* npairs */\
    asm("addl  $" STR(SLOT_CONST) ",%edi");	/* offset of the table */\
		/* edi = addr. of the table */\
    LUSW_DEBUG1(KEY);\
    asm(\
	"testl %eax,%eax\n\t"\
      LABEL "_loop:\n\t"\
	"jz    " LABEL "_loopend\n\t"\
	"cmpl  (%edi)," #KEY "\n\t"\
	"je    " LABEL "_loopend\n\t"\
	"addl  $12,%edi\n\t"\
	"decl  %eax\n\t"\
	"jmp   " LABEL "_loop\n\t"\
      LABEL "_loopend:");\
    LUSW_DEBUG2;\
    asm("movl  4(%edi),%eax");\
    \
    asm("addl  (%esp),%eax\n\t"	/* eax += mb->CompiledCode */\
	"addl  $4,%esp");\
		/* eax = tgt addr. */\
    \
    asm("jmp   *8(%edi)")


  CODE(opc_lookupswitch, lookupswitch, ST0, ST0, OPC_JUMP) {
    asm("popl  %edx");	// now state 1
    LUSW(%edx, "lusw_st0");
  }
  CODE(opc_lookupswitch, lookupswitch, ST1, ST0, OPC_JUMP) {
    LUSW(%edx, "lusw_st1");
  }
  CODE(opc_lookupswitch, lookupswitch, ST2, ST1, OPC_JUMP) {
    LUSW(%ecx, "lusw_st2");
  }
  CODE(opc_lookupswitch, lookupswitch, ST3, ST0, OPC_JUMP) {
    LUSW(%ecx, "lusw_st3");
  }
  CODE(opc_lookupswitch, lookupswitch, ST4, ST3, OPC_JUMP) {
    LUSW(%edx, "lusw_st4");
  }


  // ireturn
	// compile: stateto1, return
  // lreturn
	// compile: stateto4, return
  // return
  CODE(opc_return, return, STANY, STATE_AFTER_RETURN, OPC_NONE) {
    VALUE_DEBUG(%edx);
    VALUE_DEBUG(%ecx);
    asm(".byte 0xe9\n\t.long " STR(SLOT_CONST));	// jmp
  }


  // getstatic
  // getstatic_quick
	// const: address
  // getstatic2
  // getstatic2_quick
	// const: address, address + 4
  // putstatic
  // putstatic_quick
	// const: address
  // putstatic2
  // putstatic2_quick
	// const: address, address + 4

#define CODE_GETSTATIC(vop, THROW_EXC) \
  CODE(opc_##vop, vop, ST0, ST1, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st0");\
    asm("movl  (" STR(SLOT_CONST) "),%edx");\
    VALUE_DEBUG(%edx);\
  }\
  CODE(opc_##vop, vop, ST1, ST2, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st1");\
    asm("movl  (" STR(SLOT_CONST) "),%ecx");\
    VALUE_DEBUG(%ecx);\
  }\
  CODE(opc_##vop, vop, ST2, ST4, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st2");\
    asm("pushl %edx\n\t"	/* now state 3 */\
	"movl  (" STR(SLOT_CONST) "),%edx");\
    VALUE_DEBUG(%edx);\
  }\
  CODE(opc_##vop, vop, ST3, ST4, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st3");\
    asm("movl  (" STR(SLOT_CONST) "),%edx");\
    VALUE_DEBUG(%edx);\
  }\
  CODE(opc_##vop, vop, ST4, ST2, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st4");\
    asm("pushl %ecx\n\t"	/* now state 1 */\
	"movl  (" STR(SLOT_CONST) "),%ecx");\
    VALUE_DEBUG(%ecx);\
  }

#define CODE_PUTSTATIC(vop, THROW_EXC) \
  CODE(opc_##vop, vop, ST0, ST0, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st0");\
    asm("popl  %edx\n\t"	/* now state 1 */\
	"movl  %edx,(" STR(SLOT_CONST) ")");\
    VALUE_DEBUG(%edx);\
  }\
  CODE(opc_##vop, vop, ST1, ST0, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st1");\
    asm("movl  %edx,(" STR(SLOT_CONST) ")");\
    VALUE_DEBUG(%edx);\
  }\
  CODE(opc_##vop, vop, ST2, ST1, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st2");\
    asm("movl  %ecx,(" STR(SLOT_CONST) ")");\
    VALUE_DEBUG(%ecx);\
  }\
  CODE(opc_##vop, vop, ST3, ST0, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st3");\
    asm("movl  %ecx,(" STR(SLOT_CONST) ")");\
    VALUE_DEBUG(%ecx);\
  }\
  CODE(opc_##vop, vop, ST4, ST3, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st4");\
    asm("movl  %edx,(" STR(SLOT_CONST) ")");\
    VALUE_DEBUG(%edx);\
  }

#define GETSTATIC2_ST0(LOW_REG, HIGH_REG) \
    asm("movl  $" STR(SLOT_CONST) ",%edi\n\t"\
	"movl  (%edi)," #LOW_REG "\n\t"\
	"movl  4(%edi)," #HIGH_REG)
#define CODE_GETSTATIC2(vop, THROW_EXC) \
  CODE(opc_##vop, vop, ST0, ST2, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st0");\
    GETSTATIC2_ST0(%ecx, %edx);\
  }\
  CODE(opc_##vop, vop, ST1, ST2, THROW_EXC) {\
    asm("pushl %edx");	/* now state 0 */\
    INITCLASS_GETSTATIC(#vop "_st1");\
    GETSTATIC2_ST0(%ecx, %edx);\
  }\
  CODE(opc_##vop, vop, ST2, ST4, THROW_EXC) {\
    asm("pushl %edx\n\t"\
	"pushl %ecx");	/* now state 0 */\
    INITCLASS_GETSTATIC(#vop "_st2");\
    GETSTATIC2_ST0(%edx, %ecx);\
  }\
  CODE(opc_##vop, vop, ST3, ST4, THROW_EXC) {\
    asm("pushl %ecx");	/* now state 0 */\
    INITCLASS_GETSTATIC(#vop "_st3");\
    GETSTATIC2_ST0(%edx, %ecx);\
  }\
  CODE(opc_##vop, vop, ST4, ST2, THROW_EXC) {\
    asm("pushl %ecx\n\t"\
	"pushl %edx");	/* now state 0 */\
    INITCLASS_GETSTATIC(#vop "_st4");\
    GETSTATIC2_ST0(%ecx, %edx);\
  }

#define PUTSTATIC2(OPTOP1_REG, OPTOP2_REG) \
    asm("movl  $" STR(SLOT_CONST) ",%edi\n\t"\
	"movl  " #OPTOP1_REG ",(%edi)\n\t"\
	"movl  " #OPTOP2_REG ",4(%edi)\n\t")
	// now state 0
#define CODE_PUTSTATIC2(vop, THROW_EXC) \
  CODE(opc_##vop, vop, ST0, ST0, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st0");\
    asm("popl  %ecx\n\t"\
	"popl  %edx");	/* now state 2 */\
    PUTSTATIC2(%ecx, %edx);\
  }\
  CODE(opc_##vop, vop, ST1, ST0, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st1");\
    asm("popl  %ecx");	/* now state 4 */\
    PUTSTATIC2(%edx, %ecx);\
  }\
  CODE(opc_##vop, vop, ST2, ST0, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st2");\
    PUTSTATIC2(%ecx, %edx);\
  }\
  CODE(opc_##vop, vop, ST3, ST0, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st3");\
    asm("popl  %edx");	/* now state 2 */\
    PUTSTATIC2(%ecx, %edx);\
  }\
  CODE(opc_##vop, vop, ST4, ST0, THROW_EXC) {\
    INITCLASS_GETSTATIC(#vop "_st4");\
    PUTSTATIC2(%edx, %ecx);\
  }

#define INITCLASS_GETSTATIC(LABEL)
	// empty

  CODE_GETSTATIC(getstatic_quick, OPC_NONE);
  CODE_PUTSTATIC(putstatic_quick, OPC_NONE);
  CODE_GETSTATIC2(getstatic2_quick, OPC_NONE);
  CODE_PUTSTATIC2(putstatic2_quick, OPC_NONE);

// stuff to rewrite code for opc_{get,put}static
#define GETSTATIC_PATCH_OFFSET	"0x2c"
#define GETSTATIC_PATCH_DATA	"0x35"

#undef INITCLASS_GETSTATIC
	// redefine INITCLASS_GETSTATIC
#if (defined(INITCLASS_IN_COMPILATION) && !defined(PATCH_ON_JUMP)) || defined(PATCH_WITH_SIGTRAP)
#  define INITCLASS_GETSTATIC(LABEL)
#else
#  define INITCLASS_GETSTATIC(LABEL) \
  asm(".short 0x9090");\
  \
  asm("movl  $" STR(SLOT_CONST) ",%edi");	/* edi = cb */\
  \
  asm("pushl %edx\n\tpushl %ecx");	/* save */\
  asm("pushl %edi");\
  asm("pushl %0" : : "m" (ee));\
  asm("call  " FUNCTION(once_InitClass) "\n\t"\
      "addl  $8,%esp");\
  asm("popl  %ecx\n\tpopl  %edx");	/* restore */\
  \
  /* exc. check */\
  asm("testl %eax,%eax\n\t"\
      "jz    " LABEL "_initclass_success");\
  SIGNAL_ERROR_JUMP();\
  \
  asm(LABEL "_initclass_success:");\
  \
  /* rewrite */\
  asm(".byte 0xe8\n\t.long 0\n\t"	/* call */\
      "popl  %edi");\
  asm("subl  $" GETSTATIC_PATCH_OFFSET ",%edi\n\t"\
      "movw  $" GETSTATIC_PATCH_DATA "eb,%ax\n\t"	/* jmp XX */\
      "xchg  %ax,(%edi)");\
  \
  asm(LABEL "_initclass_done:")
#endif	// (INITCLASS_IN_COMPILATION && !PATCH_ON_JUMP) || PATCH_WITH_SIGTRAP

  CODE_GETSTATIC(getstatic, OPC_THROW);
  CODE_PUTSTATIC(putstatic, OPC_THROW);
  CODE_GETSTATIC2(getstatic2, OPC_THROW);
  CODE_PUTSTATIC2(putstatic2, OPC_THROW);


  // getfield
	// const: slot
#ifndef NO_NULL_AND_ARRAY_CHECK
#  define FIELD_ACC(HANDLE, VOP, STATE) \
    asm("movl  $" STR(SLOT_CONST) ",%eax");\
	/* slot: fb->u.offset / sizeof(OBJECT) */\
    NULL_TEST(HANDLE, #VOP "_st" #STATE "_1")
#else
#  define FIELD_ACC(HANDLE, VOP, STATE) \
    asm("movl  $" STR(SLOT_CONST) ",%eax")
	// fb->u.offset / sizeof(OBJECT)
#endif	// NO_NULL_AND_ARRAY_CHECK
	// eax = index

  CODE(opc_getfield, getfield, ST0, ST3, OPC_SIGNAL) {
    asm("popl  %edx");	// now state 1
    FIELD_ACC(%edx, getfield, 0);
    METAVM_GETFIELD(%edx, %eax, %ecx, "getfield_st0", 0);
    OBJ_GETSLOT(%edx, %eax, %ecx);
    asm("getfield_st0_done:");
    ILOAD_DEBUG1(%ecx);
  }
  CODE(opc_getfield, getfield, ST1, ST3, OPC_SIGNAL) {
    FIELD_ACC(%edx, getfield, 1);
    METAVM_GETFIELD(%edx, %eax, %ecx, "getfield_st1", 0);
    OBJ_GETSLOT(%edx, %eax, %ecx);
    asm("getfield_st1_done:");
    ILOAD_DEBUG1(%ecx);
  }
  CODE(opc_getfield, getfield, ST2, ST2, OPC_SIGNAL) {
    FIELD_ACC(%ecx, getfield, 2);
    METAVM_GETFIELD(%ecx, %eax, %ecx, "getfield_st2", 1);
    OBJ_GETSLOT(%ecx, %eax, %ecx);
    asm("getfield_st2_done:");
    ILOAD_DEBUG1(%ecx);
  }
  CODE(opc_getfield, getfield, ST3, ST1, OPC_SIGNAL) {
    FIELD_ACC(%ecx, getfield, 3);
    METAVM_GETFIELD(%ecx, %eax, %edx, "getfield_st3", 0);
    OBJ_GETSLOT(%ecx, %eax, %edx);
    asm("getfield_st3_done:");
    ILOAD_DEBUG1(%edx);
  }
  CODE(opc_getfield, getfield, ST4, ST4, OPC_SIGNAL) {
    FIELD_ACC(%edx, getfield, 4);
    METAVM_GETFIELD(%edx, %eax, %edx, "getfield_st4", 3);
    OBJ_GETSLOT(%edx, %eax, %edx);
    asm("getfield_st4_done:");
    ILOAD_DEBUG1(%edx);
  }

	// const: slot
  CODE(opc_getfield2, getfield2, ST0, ST2, OPC_SIGNAL) {
    asm("popl  %edx");	// now state 1
    FIELD_ACC(%edx, getfield2, 0);
    METAVM_GETFIELD2(%edx, %eax, %ecx, %edx, "getfield2_st0");
    OBJ_GETSLOT2(%edx, %eax, %ecx, %edx);
    asm("getfield2_st0_done:");
    LLOAD_DEBUG1(%ecx, %edx);
  }
  CODE(opc_getfield2, getfield2, ST1, ST2, OPC_SIGNAL) {
    FIELD_ACC(%edx, getfield2, 1);
    METAVM_GETFIELD2(%edx, %eax, %ecx, %edx, "getfield2_st1");
    OBJ_GETSLOT2(%edx, %eax, %ecx, %edx);
    asm("getfield2_st1_done:");
    LLOAD_DEBUG1(%ecx, %edx);
  }
  CODE(opc_getfield2, getfield2, ST2, ST4, OPC_SIGNAL) {
    asm("pushl %edx");	// now state 3
    FIELD_ACC(%ecx, getfield2, 2);
    METAVM_GETFIELD2(%ecx, %eax, %edx, %ecx, "getfield2_st2");
    OBJ_GETSLOT2(%ecx, %eax, %edx, %ecx);
    asm("getfield2_st2_done:");
    LLOAD_DEBUG1(%edx, %ecx);
  }
  CODE(opc_getfield2, getfield2, ST3, ST4, OPC_SIGNAL) {
    FIELD_ACC(%ecx, getfield2, 3);
    METAVM_GETFIELD2(%ecx, %eax, %edx, %ecx, "getfield2_st3");
    OBJ_GETSLOT2(%ecx, %eax, %edx, %ecx);
    asm("getfield2_st3_done:");
    LLOAD_DEBUG1(%edx, %ecx);
  }
  CODE(opc_getfield2, getfield2, ST4, ST2, OPC_SIGNAL) {
    asm("pushl  %ecx");	// now state 1
    FIELD_ACC(%edx, getfield2, 4);
    METAVM_GETFIELD2(%edx, %eax, %ecx, %edx, "getfield2_st4");
    OBJ_GETSLOT2(%edx, %eax, %ecx, %edx);
    asm("getfield2_st4_done:");
    LLOAD_DEBUG1(%ecx, %edx);
  }


  // putfield
	// const: slot
#define PUTFIELD_ST24(OPTOP1_REG, OPTOP2_REG, STATE) \
    FIELD_ACC(OPTOP2_REG, putfield, STATE);\
    METAVM_PUTFIELD(OPTOP2_REG, %eax, OPTOP1_REG, "putfield_st" #STATE, STATE);\
    OBJ_SETSLOT(OPTOP2_REG, %eax, OPTOP1_REG);\
    asm("putfield_st" #STATE "_done:")

  CODE(opc_putfield, putfield, ST0, ST0, OPC_SIGNAL) {
    asm("popl  %ecx\n\t"
	"popl  %edx");	// now state 2
    PUTFIELD_ST24(%ecx, %edx, 0);
    ILOAD_DEBUG1(%ecx);
  }
  CODE(opc_putfield, putfield, ST1, ST0, OPC_SIGNAL) {
    asm("popl  %ecx");	// now state 4
    PUTFIELD_ST24(%edx, %ecx, 1);
    ILOAD_DEBUG1(%edx);
  }
  CODE(opc_putfield, putfield, ST2, ST0, OPC_SIGNAL) {
    PUTFIELD_ST24(%ecx, %edx, 2);
    ILOAD_DEBUG1(%ecx);
  }
  CODE(opc_putfield, putfield, ST3, ST0, OPC_SIGNAL) {
    asm("popl  %edx");	// now state 2
    PUTFIELD_ST24(%ecx, %edx, 3);
    ILOAD_DEBUG1(%ecx);
  }
  CODE(opc_putfield, putfield, ST4, ST0, OPC_SIGNAL) {
    PUTFIELD_ST24(%edx, %ecx, 4);
    ILOAD_DEBUG1(%edx);
  }

	// const: slot
#define PUTFIELD2_ST24(OPTOP1_REG, OPTOP2_REG, STATE) \
    asm("popl  %edi");	/* edi = handle */\
    FIELD_ACC(%edi, putfield2, STATE);\
    METAVM_PUTFIELD2(%edi, %eax, OPTOP1_REG, OPTOP2_REG, "putfield2_st" #STATE, STATE);\
    OBJ_SETSLOT2(%edi, %eax, OPTOP1_REG, OPTOP2_REG);\
    asm("putfield2_st" #STATE "_done:");\
    \
    LLOAD_DEBUG1(OPTOP1_REG, OPTOP2_REG)

  CODE(opc_putfield2, putfield2, ST0, ST0, OPC_SIGNAL) {
    asm("popl  %ecx\n\t"
	"popl  %edx");	// now state 2
    PUTFIELD2_ST24(%ecx, %edx, 0);
  }
  CODE(opc_putfield2, putfield2, ST1, ST0, OPC_SIGNAL) {
    asm("popl  %ecx");	// now state 4
    PUTFIELD2_ST24(%edx, %ecx, 1);
  }
  CODE(opc_putfield2, putfield2, ST2, ST0, OPC_SIGNAL) {
    PUTFIELD2_ST24(%ecx, %edx, 2);
  }
  CODE(opc_putfield2, putfield2, ST3, ST0, OPC_SIGNAL) {
    asm("popl  %edx");	// now state 2
    PUTFIELD2_ST24(%ecx, %edx, 3);
  }
  CODE(opc_putfield2, putfield2, ST4, ST0, OPC_SIGNAL) {
    PUTFIELD2_ST24(%edx, %ecx, 4);
  }


  // invokevirtual
	// const: args_size, slot, retsize
  // invokespecial
	// const: args_size, method, local_var_space, retsize
  // invokestatic
  // invokestatic_quick
	// const: args_size, method, local_var_space, retsize
  // invokeinterface
	// const: args_size, guessptr, imethod, retsize

// stuff to rewrite code for opc_invokestatic
#  define INVOKESTATIC_PATCH_OFFSET	"0x28"
#  define INVOKESTATIC_PATCH_DATA	"0x33"

#if (defined(INITCLASS_IN_COMPILATION) && !defined(PATCH_ON_JUMP)) || defined(PATCH_WITH_SIGTRAP)
#  define INITCLASS_INVOKESTATIC(LABEL)
#else
#  define INITCLASS_INVOKESTATIC(LABEL) \
	/* edx is clazz */\
    asm(".short 0x9090");\
    \
    asm("pushl %eax\n\tpushl %ecx");	/* save */\
    asm("pushl %edx");\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    asm("call  " FUNCTION(once_InitClass) "\n\t"\
	"addl  $4,%esp");\
    asm("popl  %edx\n\tpopl  %ecx");	/* restore */\
    \
    /* exc. check */\
    asm("testl %eax,%eax\n\t"\
	"popl  %eax\n\t"		/* restore */\
	"jz    " LABEL "_initclass_success");\
    SIGNAL_ERROR_JUMP();\
    \
    asm(LABEL "_initclass_success:");\
    \
    /* rewrite */\
    asm(".byte 0xe8\n\t.long 0\n\t"	/* call */\
	"popl  %edi");\
    asm("subl  $" INVOKESTATIC_PATCH_OFFSET ",%edi\n\t"\
	"pushl %eax\n\t"\
	"movw  $" INVOKESTATIC_PATCH_DATA "eb,%ax\n\t"	/* jmp XX */\
	"xchg  %ax,(%edi)\n\t"\
	"popl  %eax");\
    \
    asm(LABEL "_initclass_done:")
#endif	// INITCLASS_IN_COMPILATION || !PATCH_ON_JUMP || PATCH_WITH_SIGTRAP


	// const: args_size
  CODE(opc_inv_head, inv_head, STANY, STSTA, OPC_NONE) {
    asm("movl  $" STR(SLOT_CONST) ",%ecx");	// ecx = args_size

    // bytepcoff = SLOT_BYTEPCOFF
    asm("movl  $" STR(SLOT_BYTEPCOFF) ",-4(%ebp)");

#ifdef RUNTIME_DEBUG
    if (runtime_debug) {
      DEBUG_IN;
      asm("movl  -4(%ebp),%eax");	// eax = bytepcoff
      asm("pushl %eax\n\tpushl %eax");
      PUSH_CONSTSTR("  pc: %d(0x%x)\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $12,%esp");
      FFLUSH;
      DEBUG_OUT;
    }
#endif
  }


#ifdef DIRECT_INVOCATION
#  define INVCORE_COMPILED \
    asm("movl  " METHOD_COMPILEDCODE(%eax) ",%ecx");\
		/* ecx = method->CompiledCode */\
    \
    asm("movl  %eax," FRAME_CURRENTMETHOD(%edi));\
		/* current_frame->current_method = callee */\
    \
    asm("call  *%ecx");\
		/* must keep:	return value (%eax)\
				return value of Java method (%edx, %ecx) */\
    \
    asm("movl  12(%ebp),%eax");			/* %eax = caller method */\
    asm("movl  %ebp," FRAME_VARS(%edi));\
		/* current_frame->vars = %ebp */\
		/* for CompiledFramePrev() */\
    asm("movl  %eax," FRAME_CURRENTMETHOD(%edi))
		/* current_frame->current_method = caller */
		/* filled registers:
			%edx & %ecx: return value of Java method
			%edi: current frame */
#  define INVCORE_INVOKE \
		/* assumption: edi is ee->current_frame, eax is method */\
		/* assumption: ACC_MACHINE_COMPILED is 0x4000 */\
    asm("testb $0x40," METHOD_ACCESS_HIGH(%eax));\
    /*asm("testw $" STR(ACC_MACHINE_COMPILED) "," METHOD_ACCESS(%eax));*/\
    asm("jz    inv_core_invoke_normal");\
    \
    INVCORE_COMPILED;\
    \
    asm("jmp   inv_core_invoke_done");\
    \
  asm("inv_core_invoke_normal:");\
    asm("call  " FUNCTION(invocationHelper));\
  asm("inv_core_invoke_done:")
#else	// DIRECT_INVOCATION
#  define INVCORE_INVOKE \
    /* cur_frame->lastpc = cur_frame->current_method->code + bytepcoff */\
    asm("movl  " FRAME_CURRENTMETHOD(%edi) ",%ecx\n\t"\
	"movl  " METHOD_CODE(%ecx) ",%ecx");\
    asm("addl  -4(%ebp),%ecx");		/* ecx = bytepcoff */\
    asm("movl  %ecx," FRAME_LASTPC(%edi));\
    \
    asm("call  " FUNCTION(invocationHelper))
#endif	// DIRECT_INVOCATION


#ifdef RUNTIME_DEBUG
#  define INVOKE_CORE_DEBUG1 \
  asm("pushl %eax");\
  if (runtime_debug) {		/* break eax */ \
    asm("movl  (%esp),%eax");	/* restore */ \
    DEBUG_IN;\
    asm("pushl %eax");\
    PUSH_CONSTSTR("  invocationHelper() returns: %d\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $8,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#else
#  define INVOKE_CORE_DEBUG1
#endif

#ifdef RUNTIME_DEBUG
#define INVOKE_CORE_PUSH_DEBUG_FLAG \
    asm("pushl %0" : : "m" (runtime_debug))
#define INVOKE_CORE_ARG_SIZE	"24"
#else
#define INVOKE_CORE_PUSH_DEBUG_FLAG
#define INVOKE_CORE_ARG_SIZE	"20"
#endif	// RUNTIME_DEBUG

#ifdef CAUSE_STACKOVERFLOW
#  define CHECK_STACK_MARGIN \
    asm("movl  -" STR(STACKOVERFLOW_MARGIN) "(%ebp),%ecx")
#else
#  define CHECK_STACK_MARGIN
#endif	// CAUSE_STACKOVERFLOW

	// const: retsize, args_size
#define INVOKE_CORE(VOP, CORE_TYPE) \
  CODE_WITHOUT_DEBUG(opc_##VOP, VOP, STANY, ST0, OPC_SIGNAL) {\
		/* CAUSE_STACKOVERFLOW needs OPC_SIGNAL */\
    DECL_GLOBAL_FUNC(SYMBOL(VOP));\
    \
    CODE_DEBUG(#VOP);\
    \
    asm("subl  %edi,%esp\n\t"\
	"pushl %edi\n\t"	/* save var. space */\
	"leal  4(%esp,%edi),%edi");		/* edi = original esp */\
    \
    asm("leal  -4(%edi,%ecx,4),%edi");	/* base of native stack */\
			/* edi = original esp + 4 * (args_size - 1) */\
    \
    /* push args of invocationHelper() */\
    INVOKE_CORE_PUSH_DEBUG_FLAG;	/* push runtime_debug */\
    asm("pushl %edi");			/* push var. base */\
    asm("movl  %0,%%edi" : : "m" (ee));	/* edi = ee */\
    asm("pushl %edi");			/* push ee */\
    asm("pushl %ecx");			/* push args_size */\
    CHECK_STACK_MARGIN;\
    asm("movl  " EE_CURRENTFRAME(%edi) ",%edi");\
		/* edi = ee->current_frame */\
    asm("pushl %eax\n\t"		/* push method */\
	"pushl %edx");			/* push obj */\
    \
    INVCORE_##CORE_TYPE;\
    \
    asm("addl  $" INVOKE_CORE_ARG_SIZE ",%esp");\
    \
    INVOKE_CORE_DEBUG1;\
    \
    asm("popl  %eax");		/* restore */\
    asm("addl  %eax,%esp");	/* free local var space */\
    \
    DECL_GLOBAL_FUNC(SYMBOL(VOP) "_done");\
		/* invoke_core_done or invoke_core_compiled_done */\
    \
    /* adjust optop */\
    asm("movl  $" STR(SLOT_CONST) ",%eax");		/* eax = args_size */\
    asm("leal  (%esp,%eax,4),%esp");		/* esp += (args_size * 4) */\
    \
    /* eax = !exceptionOccurred(ee) */\
    asm("movl  %0,%%edi" : : "m" (ee));		/* edi = ee */\
    EE_EXCEPTIONKIND_EAX(%edi);\
    asm("testl %eax,%eax\n\t"\
	".short 0x850f\n\t.long " STR(SLOT_ADDR_EXC));	/* jnz */\
  }

  INVOKE_CORE(invoke_core, INVOKE);
#ifdef EAGER_COMPILATION
  INVOKE_CORE(invoke_core_compiled, COMPILED);
#endif


  CODE(opc_inv_vir_obj, inv_vir_obj, STANY, STSTA, OPC_NONE) {
    asm("movl  -4(%esp,%ecx,4),%edx");	// ecx must be args_size
    OBJ_DEBUG(%edx);
    NULL_TEST(%edx, "inv_vir_obj");
  }

#ifdef METAVM
#  define METAVM_INVOKEVIRTUAL(LABEL) \
    JUMP_IF_NOT_REMOTE(LABEL "_not_proxy");	/* break edi */\
    \
    METHODTABLE_OF_PROXY(%edi);\
    asm("cmpl  %eax,%edi\n\t"\
	"jnz   " LABEL "_not_proxy");\
    PROXY_CLAZZ(%edx, %eax);		/* eax = Proxy.clazz */\
    CB_METHODTABLE(%eax, %eax);		/* eax = methodtable of Proxy.clazz */\
    \
    asm(LABEL "_not_proxy:")
#else
#  define METAVM_INVOKEVIRTUAL(LABEL)
#endif

	// const: slot
  CODE(opc_invokevirtual, invokevirtual, STANY, STSTA, OPC_SIGNAL) {
    OBJ_ARRAY_METHODTABLE_TO_EAX(%edx, "invokevir");
	// may cause SIGSEGV
    METAVM_INVOKEVIRTUAL("invvir");
    asm("movl  $" STR(SLOT_CONST) ",%edi");		// edi = slot
    MT_SLOT(%eax, %edi, %eax);			// eax = method
    METHOD_DEBUG(%eax, "invokevirtual");
  }
	// const: slot
  CODE(opc_invokevirtual_obj, invokevirtual_obj, STANY, STSTA, OPC_SIGNAL) {
    OBJ_METHODTABLE(%edx, %eax);
	// may cause SIGSEGV
    METAVM_INVOKEVIRTUAL("invvir_obj");
    asm("movl  $" STR(SLOT_CONST) ",%edi");		// edi = slot
    MT_SLOT(%eax, %edi, %eax);			// eax = method
    METHOD_DEBUG(%eax, "invokevirtual_obj");
  }

  CODE(opc_inv_vir_varspace, inv_vir_varspace, STANY, STSTA, OPC_NONE) {
    // allocate local var space 
    METHOD_NLOCALS(%eax, %edi);		// edi = method->nlocals
#if 1
    asm("shll  $2,%edi");
#else
    asm("testl %edi,%edi\n\t"
	"jz    inv_vir_varspace_nlocal_done\n\t"	// edi = 0, if true
		// method->nlocals of native method is 0
	"subl  %ecx,%edi\n\t"		// edi -= args_size
	"shll  $2,%edi\n\t"		// edi *= 4
      "inv_vir_varspace_nlocal_done:");
#endif
  }

#ifdef METAVM
  CODE(opc_inv_metavm, inv_metavm, STANY, STSTA, OPC_NONE) {
    JUMP_IF_NOT_PROXY(%edx /* is obj */, "inv_metavm_inv_local");
    JUMP_IF_NOT_REMOTE("inv_metavm_inv_local");
	// these break edi

    // call Proxy#* at local
    asm("pushl %eax\n\t"		// save
	"leal  4(%esp),%ecx");		// save original esp to ecx

    asm("movl  " METHOD_CLAZZ(%eax) ",%eax");
    CB_METHODTABLE(%eax,%eax);	// break edi
	// eax = cbMethodTable(mb->fb.clazz)
    METHODTABLE_OF_PROXY(%edi);
	// edi = cbMethodTable(Proxy clazz)
    asm("cmpl  %eax,%edi");
    asm("jnz   inv_metavm_inv_remote\n\t"
	"popl  %eax\n\t"	// restore
	"jmp   inv_metavm_inv_local");

    asm("inv_metavm_inv_remote:\n\t"
	"popl  %eax");		// restore

    asm("pushl %ecx\n\t"	// original stack pointer
	"pushl %eax\n\t"			// methodblock
	"pushl %edx");				// obj (Proxy)
    asm("pushl %0" : : "m" (ee));		// ee
    asm("call  " FUNCTION(proxy_invoke) "\n\t"
	"addl  $16,%esp");

    asm(".byte 0xe9\n\t.long " STR(SLOT_CONST));	// jump to invoke_core_done

    asm("inv_metavm_inv_local:");
  }
#endif	// METAVM


  CODE(opc_inv_spe_obj, inv_spe_obj, STANY, STSTA, OPC_SIGNAL) {
    asm("movl  -4(%esp,%ecx,4),%edx");	// ecx must be args_size
    OBJ_DEBUG(%edx);
    NULL_TEST(%edx, "inv_spe_obj");
    asm("movl  (%edx),%eax");	// to cause SIGSEGV
  }

	// const: method
  CODE(opc_invokespecial, invokespecial, STANY, STSTA, OPC_NONE) {
    asm("movl  $" STR(SLOT_CONST) ",%eax");
    METHOD_DEBUG(%eax, "invokespecial");
  }

	// const: local_var_space
  CODE(opc_inv_spe_varspace, inv_spe_varspace, STANY, STSTA, OPC_NONE) {
    asm("movl  $" STR(SLOT_CONST) ",%edi");
  }

  CODE(opc_inv_stq_obj, inv_stq_obj, STANY, STSTA, OPC_NONE) {
    // null instead of 0 clear (asm("xorl  %edx,%edx"))
  }

	// const: method
  CODE(opc_invokestatic_quick, invokestatic_quick, STANY, STSTA, OPC_NONE) {
    // edx = cbHandle(method->fb.clazz)
    asm("movl  $" STR(SLOT_CONST) ",%eax\n\t"
	"movl  " METHOD_CLAZZ(%eax) ",%edx");
    METHOD_DEBUG(%eax, "invokestatic_quick");
  }

	// const: local_var_space
  CODE(opc_inv_stq_varspace, inv_stq_varspace, STANY, STSTA, OPC_NONE) {
    asm("movl  $" STR(SLOT_CONST) ",%edi");
  }

  // inv_sta_obj is same as inv_stq_obj

	// const: method
  CODE(opc_invokestatic, invokestatic, STANY, STSTA, OPC_THROW) {
    // edx = cbHandle(method->fb.clazz)
    asm("movl  $" STR(SLOT_CONST) ",%eax\n\t"
	"movl  " METHOD_CLAZZ(%eax) ",%edx");
    METHOD_DEBUG(%eax, "invokestatic");

    INITCLASS_INVOKESTATIC("invokesta");
  }

  // inv_sta_varspace is same as inv_stq_varspace


  // inv_int_obj is same as inv_spe_obj

#define INVINTF_PATCH_OFFSET	"0x17"
#define INVINTF_PATCH_DATA	"0x22"

#define INVINTF_PATCH1() \
    asm(".short 0x9090")

#define INVINTF_PATCH2() \
    asm(".byte 0xe8\n\t.long 0\n\t"	/* call */\
	"popl  %edi");	/* here, can break edi */\
    asm("subl  $" INVINTF_PATCH_OFFSET ",%edi\n\t"\
	"pushl %eax\n\t"\
	"movw  $" INVINTF_PATCH_DATA "eb,%ax\n\t"	/* jmp XX */\
	"xchg  %ax,(%edi)\n\t"\
	"popl  %eax");

	// const: guessptr, imethod
  CODE(opc_invokeinterface, invokeinterface, STANY, STSTA, OPC_THROW) {
    // get methodblock: call getInterfaceMethod()

#ifdef INVINTF_INLINE_CACHE
    // eax = obj->methodtable->classdescriptor
    OBJ_METHODTABLE(%edx, %eax);
    asm("movl  $0,%edi");	// edi = cached key (ClassClass *)
    MT_CLASSDESCRIPTOR(%eax, %eax);

    asm("cmpl  %eax,%edi\n\t"
	"jne   invokeint_not_cached\n\t"
	"movl  $0,%eax\n\t"	// eax = cached method
	"jmp   invokeint_getintf_done\n\t"
      "invokeint_not_cached:\n\t"
	".byte 0xe8\n\t.long 0\n\t"	// call
	"popl  %edi");			// edi points this instruction itself
#endif	// INVINTF_INLINE_CACHE

    asm("pushl %ecx");	// save
#ifdef RUNTIME_DEBUG
    asm("pushl %0" : : "m" (runtime_debug));
#endif
#ifdef INVINTF_INLINE_CACHE
    asm("pushl %edi");			// for inlined cache
#endif
    asm("pushl $" STR(SLOT_CONST) "\n\t"	// guessptr
	"pushl $" STR(SLOT_CONST) "\n\t"	// imethod
	"pushl %0\n\t"			// ee
	"pushl %%edx" : : "m" (ee));	// obj, save
    asm("call  " FUNCTION(getInterfaceMethod) "\n\t"
    // here code is patched with `INT 3' if PATCH_WITH_SIGTRAP is defined
	"popl %edx\n\t"	// restore
#ifdef INVINTF_INLINE_CACHE
#  ifdef RUNTIME_DEBUG
	"addl  $20,%esp\n\t"
#  else
	"addl  $16,%esp\n\t"
#  endif
#else
#  ifdef RUNTIME_DEBUG
	"addl  $16,%esp\n\t"
#  else
	"addl  $12,%esp\n\t"
#  endif
#endif
    );
    asm("popl  %ecx");	// restore

#ifndef PATCH_WITH_SIGTRAP
    INVINTF_PATCH1();
    asm("testl %eax,%eax\n\t"
	"jnz   invokeint_getintf_success");
    SIGNAL_ERROR_JUMP();

    asm("invokeint_getintf_success:");
    INVINTF_PATCH2();	// break edi
#endif

    asm("invokeint_getintf_done:");
  }

  // inv_int_varspace is same as inv_vir_varspace


#ifdef ELIMINATE_TAIL_RECURSION
	// const: args_size
  CODE(opc_invoke_recursive, invoke_recursive, ST0, ST0, OPC_JUMP) {
    asm("movl  $" STR(SLOT_CONST) ",%ecx");	// ecx = args_size

    asm("movl  %esi,%edx\n\t"		// save esi
	"movl  %esi,%edi\n\t"		// target addr.
	"movl  %ecx,%eax\n\t"		// save args_size
	"leal  -4(%esp,%ecx,4),%esi\n\t"// source addr.
	"std\n\t"			// set direction flag
	".short 0xa5f3\n\t"	// "rep movsl (%esi),(%edi)\n\t"
	"leal  (%esp,%eax,4),%esp\n\t"	// esp += 4 * args_size
	"movl  %edx,%esi");		// restore esi
    asm(".byte 0xe9\n\t.long " STR(SLOT_CONST));	// jmp
  }

  CODE(opc_invoke_recursive_1, invoke_recursive_1, ST0, ST0, OPC_JUMP) {
    asm("movl  (%esp),%eax\n\t"
	"addl  $4,%esp\n\t"
	"movl  %eax,(%esi)\n\t"
	".byte 0xe9\n\t.long " STR(SLOT_CONST));	// jmp
  }

  CODE(opc_invoke_recursive_2, invoke_recursive_2, ST0, ST0, OPC_JUMP) {
    asm("movl  (%esp),%eax\n\t"
	"movl  4(%esp),%edi\n\t"
	"addl  $8,%esp\n\t"
	"movl  %eax,-4(%esi)\n\t"
	"movl  %edi,(%esi)\n\t"
	".byte 0xe9\n\t.long " STR(SLOT_CONST));	// jmp
  }

  CODE(opc_invoke_recursive_3, invoke_recursive_3, ST0, ST0, OPC_JUMP) {
    asm("movl  (%esp),%eax\n\t"
	"movl  4(%esp),%edi\n\t"
	"movl  8(%esp),%ecx\n\t"
	"addl  $12,%esp\n\t"
	"movl  %eax,-8(%esi)\n\t"
	"movl  %edi,-4(%esi)\n\t"
	"movl  %ecx,(%esi)\n\t"
	".byte 0xe9\n\t.long " STR(SLOT_CONST));	// jmp
  }
#endif	// ELIMINATE_TAIL_RECURSION


#ifdef METHOD_INLINING
  // adjust  esp  and  esi (var_base)  for the inlined method
	// const: 4 * (args_size - 1), -4 * (nlocals - 1)
  CODE(opc_inlined_enter, inlined_enter, ST0, ST0, OPC_NONE) {
    asm("leal  " STR(SLOT_CONST) "(%esp),%edi\n\t"	// eax = base of variables
	"leal  " STR(SLOT_CONST) "(%edi),%esp");	// esp = eax - 4 * (nlocals -1)
		// esi: original esp + 4 * (args_size - 1)
		// esp: esi - 4 * (nlocals - 1)
#  ifdef RUNTIME_DEBUG
    if (runtime_debug) {
      DEBUG_IN;
      asm("pushl %esi");
      PUSH_CONSTSTR("  saved var base: %x\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $8,%esp");
      DEBUG_OUT;
    }
#  endif
    asm("pushl %esi\n\t"			// save esi
	"movl  %edi,%esi");
  }

	// const: -4 * (nlocals - 1 + <# of saved values>)
  CODE(opc_inlined_exit, inlined_exit, STANY, STSTA, OPC_NONE) {
    asm("movl  %esi,%eax\n\t"
	"leal  " STR(SLOT_CONST) "(%esi),%esp\n\t"
	"popl  %esi\n\t"
	"leal  4(%eax),%esp");
#  ifdef RUNTIME_DEBUG
    if (runtime_debug) {
      DEBUG_IN;
      asm("pushl %esi");
      PUSH_CONSTSTR("  restored var base: %x\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $8,%esp");
      DEBUG_OUT;
    }
#  endif
  }


#  if defined(PATCH_ON_JUMP) && !defined(PATCH_WITH_SIGTRAP) && !defined(INITCLASS_IN_COMPILATION)
	// const: cb
  CODE(opc_init_class, init_class, STANY, STSTA, OPC_THROW) {
    INITCLASS_GETSTATIC("init_class");
  }
#  endif
#endif	// METHOD_INLINING


  // xxxunusedxxx
  CODE(opc_xxxunusedxxx, xxxunusesxxx, STANY, STSTA, OPC_NONE) {}


  // new
	// cosnt: cb
#ifdef METAVM
#  define METAVM_NEW(CB_REG, DST_REG, LABEL, STATE) \
    JUMP_IF_NOT_REMOTE(LABEL "_local");\
    \
    /* local operation if remote VM addr is null */\
    asm("movl  %0,%%edi" : : "m" (ee));	/* edi = ee */\
    asm("movl  " EE_REMOTE_ADDR(%edi) ",%eax");\
    asm("testl %eax,%eax\n\t"\
	"jz    " LABEL "_local");\
    \
    /* call isByValue() */\
    FUNCCALL_IN(STATE);\
    asm("pushl " #CB_REG "\n\t"\
	"pushl %edi\n\t"\
	"call  " FUNCTION(isByValue));\
    asm("popl  %edi\n\t"\
	"popl  " #CB_REG);\
    FUNCCALL_OUT(STATE);\
    asm("testl %eax,%eax\n\t"\
	"jnz   " LABEL "_local");\
    \
    /* call proxy_new() */\
    FUNCCALL_IN(STATE);\
    asm("pushl " #CB_REG "\n\t"\
	"pushl " EE_REMOTE_ADDR(%edi) "\n\t"\
	"movl  " EE_CURRENTFRAME(%edi) ",%eax\n\t"\
	"movl  " FRAME_CURRENTMETHOD(%eax) ",%eax\n\t"\
	"movl  " METHOD_CLAZZ(%eax) ",%eax\n\t"\
	"pushl %eax\n\t"\
		/* ee->current_frame->current_method->fb.clazz */\
	"pushl %edi\n\t"	/* ee */\
	"call  " FUNCTION(proxy_new) "\n\t"\
	"popl  %edi\n\t"	/* edi = ee */\
	"addl  $12,%esp");\
    FUNCCALL_OUT(STATE);\
    \
    JUMP_IF_EXC_HASNT_OCCURRED(%edi /* is ee */, LABEL "_done");\
DEBUG_IN;\
PUSH_CONSTSTR("METAVM_NEW exc. occurred.\n");\
asm("call  " FUNCTION(printf) "\n\t"\
    "addl  $4,%esp");\
FFLUSH;\
DEBUG_OUT;\
    SIGNAL_ERROR_JUMP();\
    asm(LABEL "_local:")
#else
#  define METAVM_NEW(CB_REG, DST_REG, LABEL, STATE)
#endif	// METAVM

// stuff to rewrite code for opc_new
#define NEW_PATCH_OFFSET	"0x2d"
#define NEW_PATCH_DATA		"0x36"

#if !defined(PATCH_ON_JUMP) || defined(PATCH_WITH_SIGTRAP)
#  define NEW_PATCH(CB_REG, LABEL, STATE)
#else
#  define NEW_PATCH(CB_REG, LABEL, STATE) \
	/* cannot omit if INITCLASS_IN_COMPILATION is defined. */\
    asm(".short 0x9090");\
    \
    asm("pushl %edx\n\tpushl %ecx");	/* save CB_REG */\
    \
    asm("movl  %0,%%edi\n\t"\
	"movl  " METHOD_CLAZZ(%%edi) ",%%edi\n\t"\
	"pushl %%edi" : : "m" (mb));	/* mb->fb.clazz */\
    asm("pushl " #CB_REG);\
    asm("pushl %0" : : "m" (ee));\
    asm("call  " FUNCTION(once_InitClass) "\n\t"\
	"addl  $12,%esp");\
    \
    asm("popl  %ecx\n\tpopl  %edx");	/* restore CB_REG */\
    \
    /* exc. check */\
    asm("testl %eax,%eax\n\t"\
	"jz    " LABEL "_once_success");\
    SIGNAL_ERROR_JUMP();\
    \
    asm(LABEL "_once_success:");\
    \
    /* rewrite */\
    asm(".byte 0xe8\n\t.long 0\n\t"	/* call */\
	"popl  %edi");\
    asm("subl  $" NEW_PATCH_OFFSET ",%edi\n\t"\
	"movw  $" NEW_PATCH_DATA "eb,%ax\n\t"	/* jmp XX */\
	"xchg  %ax,(%edi)");\
    \
    asm(LABEL "_once_done:")
#endif

#define NEW(CB_REG, DST_REG, LABEL, STATE) \
    asm("movl  $" STR(SLOT_CONST) "," #CB_REG);	/* cb */\
    \
    NEW_PATCH(CB_REG, LABEL, STATE);\
    \
    CLAZZ_DEBUG(CB_REG);\
    \
    /* instantiate */\
    METAVM_NEW(CB_REG, DST_REG, LABEL, STATE);\
    /* call newobject() */\
    FUNCCALL_IN(STATE);\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    asm("pushl $0\n\t"			/* pc */\
	"pushl " #CB_REG "\n\t"\
	"call  " FUNCTION(newobject) "\n\t"\
	"addl  $12,%esp");\
    FUNCCALL_OUT(STATE);\
    OBJ_DEBUG(%eax);\
    asm(LABEL "_done:");\
    asm("movl  %eax," #DST_REG)


  CODE(opc_new, new, ST0, ST1, OPC_THROW) {
    NEW(%edx, %edx, "new_st0", 0);
  }
  CODE(opc_new, new, ST1, ST2, OPC_THROW) {
    NEW(%ecx, %ecx, "new_st1", 1);
  }
  CODE(opc_new, new, ST2, ST4, OPC_THROW) {
    asm("pushl %edx");	// now state 3
    NEW(%edx, %edx, "new_st2", 3);
  }
  CODE(opc_new, new, ST3, ST4, OPC_THROW) {
    NEW(%edx, %edx, "new_st3", 3);
  }
  CODE(opc_new, new, ST4, ST2, OPC_THROW) {
    asm("pushl %ecx");	// now state 1
    NEW(%ecx, %ecx, "new_st4", 1);
  }


  // newarray
	// const: type
#ifndef NO_NULL_AND_ARRAY_CHECK
#  define NEWARRAY_TEST(OPTOP1_REG, LABEL) \
    asm("testl " #OPTOP1_REG "," #OPTOP1_REG "\n\t"\
	"jge   " LABEL "_test_done");\
    SIGNAL_ERROR0(EXCID_NegativeArraySizeException);\
    asm(LABEL "_test_done:")
#else
#  define NEWARRAY_TEST(OPTOP1_REG, LABEL)
#endif

#ifdef RUNTIME_DEBUG
#  define NEWARRAY_DEBUG1(TYPE, COUNT) \
  asm("pushl %eax");\
  if (runtime_debug) {\
    DEBUG_IN;\
    asm("pushl " #COUNT "\n\tpushl " #TYPE);\
    PUSH_CONSTSTR("  type: 0x%x, count: %d\n");\
    asm("call  " FUNCTION(printf) "\n\t"\
	"addl  $12,%esp");\
    FFLUSH;\
    DEBUG_OUT;\
  }\
  asm("popl  %eax")
#else
#  define NEWARRAY_DEBUG1(TYPE, COUNT)
#endif

#if defined(METAVM) && !defined(METAVM_NO_ARRAY)
#  define METAVM_NEWARRAY(TYPE, COUNT, LABEL, STATE) \
    JUMP_IF_NOT_REMOTE(LABEL "_local");\
    \
    FUNCCALL_IN(STATE);\
    asm("pushl " #COUNT "\n\t"\
	"pushl " #TYPE);\
    \
    /* local operation if remote VM addr is null */\
    asm("movl  %0,%%edi" : : "m" (ee));	/* edi = ee */\
    asm("movl  " EE_REMOTE_ADDR(%edi) ",%eax");\
    asm("testl %eax,%eax\n\t"\
	"jz    " LABEL "_addr_null");\
    \
    asm("pushl %eax\n\t"	/* addr */\
	"movl  " EE_CURRENTFRAME(%edi) ",%eax\n\t"\
	"movl  " FRAME_CURRENTMETHOD(%eax) ",%eax\n\t"\
	"movl  " METHOD_CLAZZ(%eax) ",%eax\n\t"\
	"pushl %eax\n\t"\
		/* ee->current_frame->current_method->fb.clazz */\
	"pushl %edi\n\t"	/* ee */\
	"call  " FUNCTION(proxy_newarray) "\n\t"\
	"popl  %edi\n\t"	/* edi = ee */\
	"addl  $16,%esp");\
    FUNCCALL_OUT(STATE);\
    \
    JUMP_IF_EXC_HASNT_OCCURRED(%edi /* is ee */, LABEL "_done");\
DEBUG_IN;\
PUSH_CONSTSTR("METAVM_NEWARRAY exc. occurred.\n");\
asm("call  " FUNCTION(printf) "\n\t"\
    "addl  $4,%esp");\
FFLUSH;\
DEBUG_OUT;\
    SIGNAL_ERROR_JUMP();\
    \
    asm(LABEL "_addr_null:\n\t"\
	"popl  " #TYPE "\n\t"	/* restore */\
	"popl  " #COUNT);	/* restore */\
    FUNCCALL_OUT(STATE);\
    asm(LABEL "_local:")
#else
#  define METAVM_NEWARRAY(TYPE, COUNT, LABEL, STATE)
#endif	// METAVM_NO_ARRAY

#define NEWARRAY(OPTOP1_REG, LABEL, STATE) \
    asm("movl  $" STR(SLOT_CONST) ",%eax");	/* eax = type */\
    NEWARRAY_DEBUG1(%eax, OPTOP1_REG);\
    \
    NEWARRAY_TEST(OPTOP1_REG, LABEL);\
    \
    METAVM_NEWARRAY(%eax, OPTOP1_REG, LABEL, STATE);\
    \
    /* call ArrayAlloc() */\
    FUNCCALL_IN(STATE);\
    ALLOC_ARRAY("%eax", OPTOP1_REG);\
    FUNCCALL_OUT(STATE);\
    asm("testl %eax,%eax\n\t"\
	"jnz   " LABEL "_done");\
    SIGNAL_ERROR0(EXCID_OutOfMemoryError);\
    asm(LABEL "_done:\n\t"\
	"movl  %eax," #OPTOP1_REG)	// store to dst.

  CODE(opc_newarray, newarray, ST0, ST1, OPC_THROW) {
    asm("popl  %edx");	// now state 1
    NEWARRAY(%edx, "newarray_st0", 1);
  }
  CODE(opc_newarray, newarray, ST1, ST1, OPC_THROW) {
    NEWARRAY(%edx, "newarray_st1", 1);
  }
  CODE(opc_newarray, newarray, ST2, ST2, OPC_THROW) {
    NEWARRAY(%ecx, "newarray_st2", 2);
  }
  CODE(opc_newarray, newarray, ST3, ST3, OPC_THROW) {
    NEWARRAY(%ecx, "newarray_st3", 3);
  }
  CODE(opc_newarray, newarray, ST4, ST4, OPC_THROW) {
    NEWARRAY(%edx, "newarray_st4", 4);
  }


  // anewarray
	// const: elem_clazz
#if defined(METAVM) && !defined(METAVM_NO_ARRAY)
#  define METAVM_ANEWARRAY(COUNT, LABEL, STATE) \
    JUMP_IF_NOT_REMOTE(LABEL "_local");\
    \
    asm("movl  %0,%%edi" : : "m" (ee));	/* edi = ee */\
    /* local operation if remote VM addr is null */\
    asm("movl  " EE_REMOTE_ADDR(%edi) ",%eax\n\t"\
	"testl %eax,%eax\n\t"\
	"jz    " LABEL "_local");\
    \
    /* call proxy_anewarray() */\
    FUNCCALL_IN(STATE);\
    asm("pushl " #COUNT "\n\t"		/* count */\
	"pushl $" STR(SLOT_CONST) "\n\t"	/* clazz of elements */\
	"pushl %eax\n\t"		/* addr */\
	"movl  " EE_CURRENTFRAME(%edi) ",%eax\n\t"\
	"movl  " FRAME_CURRENTMETHOD(%eax) ",%eax\n\t"\
	"movl  " METHOD_CLAZZ(%eax) ",%eax\n\t"\
	"pushl %eax\n\t"\
		/* ee->current_frame->current_method->fb.clazz */\
	"pushl %edi\n\t"		/* ee */\
	"call  " FUNCTION(proxy_anewarray) "\n\t"\
	"popl  %edi\n\t"		/* edi = ee */\
	"addl  $16,%esp");\
    FUNCCALL_OUT(STATE);\
    \
    JUMP_IF_EXC_HASNT_OCCURRED(%edi /* is ee */, LABEL "_done");\
DEBUG_IN;\
PUSH_CONSTSTR("METAVM_ANEWARRAY exc. occurred.\n");\
asm("call  " FUNCTION(printf) "\n\t"\
    "addl  $4,%esp");\
FFLUSH;\
DEBUG_OUT;\
    SIGNAL_ERROR_JUMP();\
    asm(LABEL "_local:")
#else
#  define METAVM_ANEWARRAY(COUNT, LABEL, STATE)
#endif	// METAVM_NO_ARRAY

#define ANEWARRAY(T, OPTOP1_REG, LABEL, STATE) \
    NEWARRAY_TEST(OPTOP1_REG, LABEL);\
    \
    METAVM_ANEWARRAY(OPTOP1_REG, LABEL, STATE);\
    \
    /* call ArrayAlloc() */\
    FUNCCALL_IN(STATE);\
    ALLOC_ARRAY("$" STR(T_CLASS), OPTOP1_REG);\
    FUNCCALL_OUT(STATE);\
    asm("testl %eax,%eax\n\t"\
	"jnz   " LABEL "_2");\
    SIGNAL_ERROR0(EXCID_OutOfMemoryError);\
    asm(LABEL "_2:");\
    \
    UNHAND(%eax, %edi);\
    asm("movl  $" STR(SLOT_CONST) ",(%edi," #OPTOP1_REG ",4)");\
	/* unhand(array)->body[count] = clazz of elements */\
    asm(LABEL "_done:\n\t"\
	"movl  %eax," #OPTOP1_REG)		/* store to dst. */

  CODE(opc_anewarray, anewarray, ST0, ST1, OPC_THROW) {
    asm("popl  %edx");	// now state 1
    ANEWARRAY(T_CLASS, %edx, "anewarray_st0", 1);
  }
  CODE(opc_anewarray, anewarray, ST1, ST1, OPC_THROW) {
    ANEWARRAY(T_CLASS, %edx, "anewarray_st1", 1);
  }
  CODE(opc_anewarray, anewarray, ST2, ST2, OPC_THROW) {
    ANEWARRAY(T_CLASS, %ecx, "anewarray_st2", 2);
  }
  CODE(opc_anewarray, anewarray, ST3, ST3, OPC_THROW) {
    ANEWARRAY(T_CLASS, %ecx, "anewarray_st3", 3);
  }
  CODE(opc_anewarray, anewarray, ST4, ST4, OPC_THROW) {
    ANEWARRAY(T_CLASS, %edx, "anewarray_st4", 4);
  }


  // arraylength
#ifdef METAVM
#  define METAVM_ARRAYLENGTH(HANDLE, DST, LABEL, STATE) \
    JUMP_IF_NOT_PROXY(HANDLE, LABEL "_local");\
    JUMP_IF_NOT_REMOTE(LABEL "_local");\
    \
    FUNCCALL_IN(STATE);\
    asm("pushl " #HANDLE);\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    asm("call  " FUNCTION(proxy_arraylength) "\n\t"\
	"movl  %eax," #DST "\n\t"\
	"addl  $8,%esp");\
    FUNCCALL_OUT(STATE);\
    \
    asm("jmp  " LABEL "_done");\
    \
    asm(LABEL "_local:")
#else
#  define METAVM_ARRAYLENGTH(HANDLE, DST, LABEL, STATE)
#endif	// METAVM

  CODE(opc_arraylength, arraylength, ST0, ST3, OPC_SIGNAL) {
    asm("popl  %edx");	// now state 1
    NULL_TEST(%edx, "arylen_null_st0");
    METAVM_ARRAYLENGTH(%edx, %ecx, "arraylength_st0", 0);
    OBJ_LENGTH(%edx, %ecx);
    asm("arraylength_st0_done:");
  }
  CODE(opc_arraylength, arraylength, ST1, ST3, OPC_SIGNAL) {
    NULL_TEST(%edx, "arylen_null_st1");
    METAVM_ARRAYLENGTH(%edx, %ecx, "arraylength_st1", 0);
    OBJ_LENGTH(%edx, %ecx);
    asm("arraylength_st1_done:");
  }
  CODE(opc_arraylength, arraylength, ST2, ST2, OPC_SIGNAL) {
    NULL_TEST(%ecx, "arylen_null_st2");
    METAVM_ARRAYLENGTH(%ecx, %ecx, "arraylength_st2", 1);
    OBJ_LENGTH(%ecx, %ecx);
    asm("arraylength_st2_done:");
  }
  CODE(opc_arraylength, arraylength, ST3, ST1, OPC_SIGNAL) {
    NULL_TEST(%ecx, "arylen_null_st3");
    METAVM_ARRAYLENGTH(%ecx, %edx, "arraylength_st3", 0);
    OBJ_LENGTH(%ecx, %edx);
    asm("arraylength_st3_done:");
  }
  CODE(opc_arraylength, arraylength, ST4, ST4, OPC_SIGNAL) {
    NULL_TEST(%edx, "arylen_null_st4");
    METAVM_ARRAYLENGTH(%edx, %edx, "arraylength_st4", 3);
    OBJ_LENGTH(%edx, %edx);
    asm("arraylength_st4_done:");
  }


  // athrow
#define ATHROW(OPTOP1_REG, STATE) \
    VALUE_DEBUG(OPTOP1_REG);\
    \
    NULL_TEST(OPTOP1_REG, "athrow_st" #STATE "_1");\
    asm("movl  (" #OPTOP1_REG "),%eax");	/* may cause SIGSEGV */\
    \
    /* macro exceptionThrow(ee, obj) */\
    {\
      register ExecEnv *cur_ee asm("eax");\
      register JHandle *obj asm(#OPTOP1_REG);\
      \
      cur_ee = ee;\
      cur_ee->exceptionKind = EXCKIND_THROW;\
      cur_ee->exception.exc = obj;\
    }\
    \
    asm("movl  $" STR(SLOT_BYTEPCOFF) ",-4(%ebp)");\
		/* bytepcoff = SLOT_BYTEPCOFF */\
    asm(".byte 0xe9\n\t.long " STR(SLOT_ADDR_EXC))
	// jmp

  CODE(opc_athrow, athrow, ST0, ST1, OPC_SIGNAL) {
    asm("popl  %edx");	// now state 1
    ATHROW(%edx, 0);
  }
  CODE(opc_athrow, athrow, ST1, ST1, OPC_SIGNAL) {
    ATHROW(%edx, 1);
  }
  CODE(opc_athrow, athrow, ST2, ST2, OPC_SIGNAL) {
    ATHROW(%ecx, 2);
  }
  CODE(opc_athrow, athrow, ST3, ST3, OPC_SIGNAL) {
    ATHROW(%ecx, 3);
  }
  CODE(opc_athrow, athrow, ST4, ST4, OPC_SIGNAL) {
    ATHROW(%edx, 4);
  }


  // checkcast
	// const: cb
#ifdef METAVM
#  define METAVM_CHECKCAST(OPTOP1_REG, LABEL, STATE) \
    FUNCCALL_IN(STATE);\
    asm("pushl %eax\n\t"	/* cb */\
	"call  " FUNCTION(isCheckPassType) "\n\t"\
	"testl %eax,%eax\n\t"\
	"popl  %eax");\
    FUNCCALL_OUT(STATE);\
    asm("jnz   " LABEL "_done");\
    \
    /* compare handle->methods with cb_of_Proxy->methodtable */\
    JUMP_IF_NOT_PROXY(OPTOP1_REG, LABEL "_local");\
    JUMP_IF_NOT_REMOTE(LABEL "_local");\
    {\
      register ClassNET_shudo_metavm_Proxy *edi asm("edi");\
      register Hjava_lang_Class *clz asm("edi");\
      \
      UNHAND(OPTOP1_REG, %edi);\
      clz = edi->clz;\
    }	/* edi = Proxy.clz */\
    FUNCCALL_IN(STATE);\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    asm("pushl %eax\n\t"		/* cb */\
	"pushl %edi\n\t"\
	"call  " FUNCTION(is_subclass_of));\
    CAST_UINT8_TO_INT32(%eax);\
    asm("addl  $12,%esp");\
    FUNCCALL_OUT(STATE);\
    \
    asm("testl %eax,%eax\n\t"\
	"jz    " LABEL "_fail\n\t"\
	"jmp   " LABEL "_done\n\t"\
      LABEL "_local:");
#else
#  define METAVM_CHECKCAST(OPTOP1_REG, LABEL, STATE)
#endif	// METAVM

#ifdef RUNTIME_DEBUG
#  define CHECKCAST_DEBUG(HANDLE, CB, LABEL) \
    asm("pushl %eax");\
    if (runtime_debug) {	/* break eax */ \
      asm("movl  (%esp),%eax");	/* restore */ \
      DEBUG_IN;\
      CB_NAME(CB, %edi);\
      asm("pushl %edi");\
      OBJ_ARRAY_METHODTABLE_TO_EAX(HANDLE, LABEL);\
      \
      asm("testl %eax,%eax\n\t"\
	  "jnz   " LABEL "_cc_debug_not_null");\
      PUSH_CONSTSTR("  methodtable is null\n");\
      asm("call  " FUNCTION(printf) "\n\t"\
	  "addl  $8,%esp\n\t"\
	  "jmp   " LABEL "_cc_debug_done");\
      \
      asm(LABEL "_cc_debug_not_null:");\
      MT_CLASSDESCRIPTOR(%eax, %eax);\
      CB_NAME(%eax, %eax);\
      asm("pushl %eax");\
      PUSH_CONSTSTR("  %s instanceof %s\n");\
      asm("call  " FUNCTION(printf) "\n\t"\
	  "addl  $12,%esp");\
      \
      asm(LABEL "_cc_debug_done:");\
      \
      FFLUSH;\
      DEBUG_OUT;\
    }\
    asm("popl  %eax")
#else
#  define CHECKCAST_DEBUG(HANDLE, CB, LABEL)
#endif

#if 0
#define CHECKCAST(HANDLE, LABEL, STATE) \
    asm("testl " #HANDLE "," #HANDLE "\n\t"\
	"jz    " LABEL "_done");\
    \
    asm("movl  $" STR(SLOT_CONST) ",%eax");	/* cb */\
    METAVM_CHECKCAST(HANDLE, LABEL, STATE);\
    CHECKCAST_DEBUG(HANDLE, %eax, LABEL);\
    \
    OBJ_METHODTABLE(HANDLE, %edi);\
    asm("testl $0x1f,%edi\n\t"\
	"jnz   " LABEL "_call");\
    MT_CLASSDESCRIPTOR(%edi, %edi);\
    asm("cmpl  %edi,%eax\n\t"\
	"je    " LABEL "_done");\
    \
    asm(LABEL "_call:");\
    FUNCCALL_IN(STATE);\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    asm("pushl %eax\n\t"		/* cb */\
	"pushl " #HANDLE "\n\t"\
	"call  " FUNCTION(is_instance_of));\
    CAST_UINT8_TO_INT32(%eax);\
    asm("popl  " #HANDLE "\n\t"\
	"addl  $8,%esp");\
    FUNCCALL_OUT(STATE);\
    \
    asm("testl %eax,%eax\n\t"\
	"jnz   " LABEL "_done\n\t"\
      LABEL "_fail:");\
    SIGNAL_ERROR0(EXCID_ClassCastException);\
    asm(LABEL "_done:")
#else
#define CHECKCAST(HANDLE, LABEL, STATE) \
    asm("testl " #HANDLE "," #HANDLE "\n\t"\
	"jz    " LABEL "_done");\
    \
    asm("movl  $" STR(SLOT_CONST) ",%eax");	/* cb */\
    METAVM_CHECKCAST(HANDLE, LABEL, STATE);\
    CHECKCAST_DEBUG(HANDLE, %eax, LABEL);\
    \
    FUNCCALL_IN(STATE);\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    asm("pushl %eax\n\t"		/* cb */\
	"pushl " #HANDLE "\n\t"\
	"call  " FUNCTION(is_instance_of));\
    CAST_UINT8_TO_INT32(%eax);\
    asm("popl  " #HANDLE "\n\t"\
	"addl  $8,%esp");\
    FUNCCALL_OUT(STATE);\
    \
    asm("testl %eax,%eax\n\t"\
	"jnz   " LABEL "_done\n\t"\
      LABEL "_fail:");\
    SIGNAL_ERROR0(EXCID_ClassCastException);\
    asm(LABEL "_done:")
#endif

  CODE(opc_checkcast, checkcast, ST0, ST1, OPC_THROW) {
    asm("popl  %edx");	// now state 1
    CHECKCAST(%edx, "checkcast_st0", 0);
  }
  CODE(opc_checkcast, checkcast, ST1, ST1, OPC_THROW) {
    CHECKCAST(%edx, "checkcast_st1", 0);
  }
  CODE(opc_checkcast, checkcast, ST2, ST2, OPC_THROW) {
    CHECKCAST(%ecx, "checkcast_st2", 1);
  }
  CODE(opc_checkcast, checkcast, ST3, ST3, OPC_THROW) {
    CHECKCAST(%ecx, "checkcast_st3", 0);
  }
  CODE(opc_checkcast, checkcast, ST4, ST4, OPC_THROW) {
    CHECKCAST(%edx, "checkcast_st4", 3);
  }


  // instanceof
	// const: cb
#ifdef METAVM
#  define METAVM_INSTANCEOF(OPTOP1_REG, LABEL, STATE) \
    FUNCCALL_IN(STATE);\
    asm("pushl %eax\n\t"	/* cb */\
	"call  " FUNCTION(isCheckPassType) "\n\t"\
	"testl %eax,%eax\n\t"\
	"popl  %eax");\
    FUNCCALL_OUT(STATE);\
    asm("jnz   " LABEL "_true");\
    \
    /* compare handle->methods with cb_of_Proxy->methodtable */\
    JUMP_IF_NOT_PROXY(OPTOP1_REG, LABEL "_local");\
    JUMP_IF_NOT_REMOTE(LABEL "_local");\
    {\
      register ClassNET_shudo_metavm_Proxy *edi asm("edi");\
      register Hjava_lang_Class *clz asm("edi");\
      \
      UNHAND(OPTOP1_REG, %edi);\
      clz = edi->clz;\
    }	/* edi = Proxy.clz */\
    FUNCCALL_IN(STATE);\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    asm("pushl %eax\n\t"		/* cb */\
	"pushl %edi\n\t"\
	"call  " FUNCTION(is_subclass_of));\
    CAST_UINT8_TO_INT32(%eax);\
    asm("addl  $12,%esp");\
    FUNCCALL_OUT(STATE);\
    \
    asm("testl %eax,%eax\n\t"\
	"jz   " LABEL "_false\n\t"\
      LABEL "_true:"\
	"movl $1," #OPTOP1_REG "\n\t"\
	"jmp  " LABEL "_done\n\t"\
      LABEL "_false:"\
	"movl $0," #OPTOP1_REG "\n\t"\
	"jmp  " LABEL "_done\n\t"\
      LABEL "_local:");
#else
#  define METAVM_INSTANCEOF(OPTOP1_REG, LABEL, STATE)
#endif	// METAVM

#define INSTANCEOF(OPTOP1_REG, LABEL, STATE) \
    asm("testl " #OPTOP1_REG "," #OPTOP1_REG "\n\t"\
	"jz    " LABEL "_done");\
    \
    asm("movl  $" STR(SLOT_CONST) ",%eax");	/* cb */\
    METAVM_INSTANCEOF(OPTOP1_REG, LABEL, STATE);\
    CHECKCAST_DEBUG(OPTOP1_REG, %eax, LABEL);\
    FUNCCALL_IN(STATE);\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    asm("pushl %eax\n\t"		/* cb */\
	"pushl " #OPTOP1_REG "\n\t"\
	"call  " FUNCTION(is_instance_of));\
    CAST_UINT8_TO_INT32(%eax);\
    asm("addl  $12,%esp");\
    asm("movl  %eax," #OPTOP1_REG);\
    FUNCCALL_OUT(STATE);\
  asm(LABEL "_done:")

  CODE(opc_instanceof, instanceof, ST0, ST1, OPC_NONE) {
    asm("popl  %edx");	// now state 1
    INSTANCEOF(%edx, "instanceof_st0", 0);
  }
  CODE(opc_instanceof, instanceof, ST1, ST1, OPC_NONE) {
    INSTANCEOF(%edx, "instanceof_st1", 0);
  }
  CODE(opc_instanceof, instanceof, ST2, ST2, OPC_NONE) {
    INSTANCEOF(%ecx, "instanceof_st2", 1);
  }
  CODE(opc_instanceof, instanceof, ST3, ST3, OPC_NONE) {
    INSTANCEOF(%ecx, "instanceof_st3", 0);
  }
  CODE(opc_instanceof, instanceof, ST4, ST4, OPC_NONE) {
    INSTANCEOF(%edx, "instanceof_st4", 3);
  }


  // monitorenter, monitorexit
#ifdef RUNTIME_DEBUG
#  define MONITOR_DEBUG \
    if (runtime_debug) {\
      DEBUG_IN;\
      PUSH_CONSTSTR("  monitor*() done.\n");\
      asm("call  " FUNCTION(printf) "\n\t"\
	  "addl  $4,%esp");\
      FFLUSH;\
      DEBUG_OUT;\
    }
#else
#  define MONITOR_DEBUG
#endif

#define MONITOR(OPTOP1_REG, FUNCNAME, METAVM_FUNCNAME, LABEL, STATE) \
    VALUE_DEBUG(OPTOP1_REG);\
    \
    NULL_TEST(OPTOP1_REG, LABEL "_1");\
    asm("movl  (" #OPTOP1_REG "),%eax");	/* may cause SIGSEGV */\
    \
    METAVM_MONITOR(OPTOP1_REG, METAVM_FUNCNAME, LABEL, STATE);\
    \
    FUNCCALL_IN(STATE);\
    OBJ_MONITOR(OPTOP1_REG);\
    CALL_MONITOR(OPTOP1_REG, FUNCNAME);\
    FUNCCALL_OUT(STATE);\
    \
    asm(LABEL "_done:");\
    MONITOR_DEBUG;

#define CODE_MONITOR(vop, FUNCNAME, METAVM_FUNCNAME) \
  CODE(opc_##vop, vop, ST0, ST0, OPC_SIGNAL) {\
    asm("popl  %edx");	/* now state 1 */\
    MONITOR(%edx, FUNCNAME, METAVM_FUNCNAME, #vop "_st0", 0);\
  }\
  CODE(opc_##vop, vop, ST1, ST0, OPC_SIGNAL) {\
    MONITOR(%edx, FUNCNAME, METAVM_FUNCNAME, #vop "_st1", 0);\
  }\
  CODE(opc_##vop, vop, ST2, ST1, OPC_SIGNAL) {\
    MONITOR(%ecx, FUNCNAME, METAVM_FUNCNAME, #vop "_st2", 1);\
  }\
  CODE(opc_##vop, vop, ST3, ST0, OPC_SIGNAL) {\
    MONITOR(%ecx, FUNCNAME, METAVM_FUNCNAME, #vop "_st3", 0);\
  }\
  CODE(opc_##vop, vop, ST4, ST3, OPC_SIGNAL) {\
    MONITOR(%edx, FUNCNAME, METAVM_FUNCNAME, #vop "_st4", 3);\
  }

#if JDK_VER >= 12
  CODE_MONITOR(monitorenter, monitorEnter2, proxy_monitorenter);
  CODE_MONITOR(monitorexit, monitorExit2, proxy_monitorexit);
#else
  CODE_MONITOR(monitorenter, monitorEnter, proxy_monitorenter);
  CODE_MONITOR(monitorexit, monitorExit, proxy_monitorexit);
#endif	// JDK_VER


  // multianewarray
	// const: dimensions, arrayclazz
#define MULTIANEWARRAY_TEST(STATE) \
    asm("cmpl  $-1,%eax\n\t"  /* eax is returned by multianewarray() */\
	"jne   mulary_st" #STATE "_1");\
    SIGNAL_ERROR0(EXCID_NegativeArraySizeException);\
    asm("mulary_st" #STATE "_1:");\
    asm("testl %eax,%eax\n\t"\
	"jnz   mulary_st" #STATE "_2");\
    SIGNAL_ERROR0(EXCID_OutOfMemoryError);\
    asm("mulary_st" #STATE "_2:")

#ifdef RUNTIME_DEBUG
#  define MULTIANEWARRAY_FUNC \
  asm("pushl %0" : : "m" (runtime_debug));\
  asm("call  " FUNCTION(multianewarray) "\n\t"\
      "addl  $20,%esp")
#else
#  define MULTIANEWARRAY_FUNC \
  asm("call  " FUNCTION(multianewarray) "\n\t"\
      "addl  $16,%esp")
#endif

#if defined(METAVM) && !defined(METAVM_NO_ARRAY)
#  define METAVM_MULTIANEWARRAY(DIM, STACKPOINTER, LABEL) \
	/* DIM: edx, STACKPOINTER: ecx */\
    JUMP_IF_NOT_REMOTE(LABEL "_local");\
    \
    asm("movl  %0,%%edi" : : "m" (ee));	/* edi = ee */\
    /* local operation if remote VM addr is null */\
    asm("movl  " EE_REMOTE_ADDR(%edi) ",%eax\n\t"\
	"testl %eax,%eax\n\t"\
	"jz    " LABEL "_local");\
    \
    FUNCCALL_IN(0);\
    asm("pushl " #DIM);	/* save */\
    \
    asm("pushl " #STACKPOINTER "\n\t"	/* stackpointer */\
	"pushl " #DIM "\n\t"		/* dim */\
	"pushl $" STR(SLOT_CONST) "\n\t"	/* arrayclazz */\
	"pushl %eax\n\t"		/* addr */\
	"movl  " EE_CURRENTFRAME(%edi) ",%eax\n\t"\
	"movl  " FRAME_CURRENTMETHOD(%eax) ",%eax\n\t"\
	"movl  " METHOD_CLAZZ(%eax) ",%eax\n\t"\
	"pushl %eax\n\t"\
		/* ee->current_frame->current_method->fb.clazz */\
	"pushl %edi\n\t"		/* ee */\
	"call  " FUNCTION(proxy_multianewarray) "\n\t"\
	"popl  %edi\n\t"		/* edi = ee */\
	"addl  $20,%esp");\
    \
    asm("popl  " #DIM);	/* restore */\
    FUNCCALL_OUT(0);\
    \
    JUMP_IF_EXC_HASNT_OCCURRED(%edi /* is ee */, LABEL "_done");\
DEBUG_IN;\
PUSH_CONSTSTR("METAVM_MULTIANEWARRAY exc. occurred.\n");\
asm("call  " FUNCTION(printf) "\n\t"\
    "addl  $4,%esp");\
FFLUSH;\
DEBUG_OUT;\
    SIGNAL_ERROR_JUMP();\
    asm(LABEL "_local:")
#else
#  define METAVM_MULTIANEWARRAY(DIM, STACKPOINTER, LABEL)
#endif	// METAVM_NO_ARRAY

#define MULTIANEWARRAY_ST0(DST_REG, LABEL, STATE) \
    asm("movl  $" STR(SLOT_CONST) ",%edx");	/* dimensions */\
    \
    asm("movl  %esp,%ecx");		/* stackpointer */\
    \
    METAVM_MULTIANEWARRAY(%edx, %ecx, LABEL);\
    \
    FUNCCALL_IN(0);\
    asm("pushl %edx");	/* save */\
    \
    asm("pushl %ecx\n\t"		/* stackpointer */\
	"pushl $" STR(SLOT_CONST) "\n\t"	/* arrayclazz */\
	"pushl %edx");			/* dimensions */\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    MULTIANEWARRAY_FUNC;\
    \
    asm("popl  %edx");	/* restore */\
    FUNCCALL_OUT(0);\
    \
    asm(LABEL "_done:");\
    asm("leal  (%esp,%edx,4),%esp");/* pop dimensions */\
    \
    MULTIANEWARRAY_TEST(STATE);\
    asm("movl  %eax," #DST_REG)

  CODE(opc_multianewarray, multianewarray, ST0, ST1, OPC_THROW) {
    MULTIANEWARRAY_ST0(%edx, "multianewarray_st0", 0);
  }
  CODE(opc_multianewarray, multianewarray, ST1, ST1, OPC_THROW) {
    asm("pushl %edx");	// now state 0
    MULTIANEWARRAY_ST0(%edx, "multianewarray_st1", 1);
  }
  CODE(opc_multianewarray, multianewarray, ST2, ST1, OPC_THROW) {
    asm("pushl %edx\n\t"
	"pushl %ecx");	// now state 0
    MULTIANEWARRAY_ST0(%edx, "multianewarray_st2", 2);
  }
  CODE(opc_multianewarray, multianewarray, ST3, ST1, OPC_THROW) {
    asm("pushl %ecx");	// now state 0
    MULTIANEWARRAY_ST0(%edx, "multianewarray_st3", 3);
  }
  CODE(opc_multianewarray, multianewarray, ST4, ST1, OPC_THROW) {
    asm("pushl %ecx\n\t"
	"pushl %edx");	// now state 0
    MULTIANEWARRAY_ST0(%edx, "multianewarray_st4", 4);
  }


  // invokeignored_quick
	// const: args_size
#define INVOKEIGNORED_QUICK_ST0(STATE) \
    asm("movl  $" STR(SLOT_CONST) ",%edi\n\t"\
	"movl  -4(%esp,%edi,4),%eax\n\t"\
	"leal  (%esp,%edi,4),%esp");\
    NULL_TEST(%eax, "invign_st" #STATE);\
    asm("movl  (%eax),%edi")
	// may cause SIGSEGV

#define INVOKEIGNORED_STATIC_QUICK_ST0(STATE) \
    asm("movl  $" STR(SLOT_CONST) ",%edi\n\t"\
	"leal  (%esp,%edi,4),%esp")

#define INVOKEIGNORED_STATIC_ST0(STATE) \
    INITCLASS_GETSTATIC("invign_nocheck_st" #STATE);\
			/* includes STR(SLOT_CONST) */\
    INVOKEIGNORED_STATIC_QUICK_ST0(STATE);

#define CODE_INVOKEIGNORED(suffix, SUFFIX, EXC_TH) \
  CODE(opc_invokeignored_##suffix, invokeignored_##suffix, ST0, ST0, EXC_TH) {\
    INVOKEIGNORED_##SUFFIX##_ST0(0);\
  }\
  CODE(opc_invokeignored_##suffix, invokeignored_##suffix, ST1, ST0, EXC_TH) {\
    asm("pushl %edx");	/* now state 0 */\
    INVOKEIGNORED_##SUFFIX##_ST0(1);\
  }\
  CODE(opc_invokeignored_##suffix, invokeignored_##suffix, ST2, ST0, EXC_TH) {\
    asm("pushl %edx\n\t"\
	"pushl %ecx");	/* now state 0 */\
    INVOKEIGNORED_##SUFFIX##_ST0(2);\
  }\
  CODE(opc_invokeignored_##suffix, invokeignored_##suffix, ST3, ST0, EXC_TH) {\
    asm("pushl %ecx");	/* now state 0 */\
    INVOKEIGNORED_##SUFFIX##_ST0(3);\
  }\
  CODE(opc_invokeignored_##suffix, invokeignored_##suffix, ST4, ST0, EXC_TH) {\
    asm("pushl %ecx\n\t"\
	"pushl %edx");	/* now state 0 */\
    INVOKEIGNORED_##SUFFIX##_ST0(4);\
  }

  CODE_INVOKEIGNORED(quick, QUICK, OPC_SIGNAL);
  CODE_INVOKEIGNORED(static_quick, STATIC_QUICK, OPC_NONE);
  CODE_INVOKEIGNORED(static, STATIC, OPC_THROW);


  // new_quick
	// const: cb
#define NEW_QUICK(DST_REG, STATE) \
    FUNCCALL_IN(STATE);\
    asm("pushl %0" : : "m" (ee));	/* ee */\
    asm("pushl $0\n\t"		/* pc */\
	"pushl $" STR(SLOT_CONST) "\n\t"\
	"call  " FUNCTION(newobject) "\n\t"\
	"addl  $12,%esp");\
    FUNCCALL_OUT(STATE);\
    asm("movl  %eax," #DST_REG)

  CODE(opc_new_quick, new_quick, ST0, ST1, OPC_NONE) {
    NEW_QUICK(%edx, 0);
  }
  CODE(opc_new_quick, new_quick, ST1, ST2, OPC_NONE) {
    NEW_QUICK(%ecx, 1);
  }
  CODE(opc_new_quick, new_quick, ST2, ST4, OPC_NONE) {
    asm("pushl %edx");	// now state 3
    NEW_QUICK(%edx, 3);
  }
  CODE(opc_new_quick, new_quick, ST3, ST4, OPC_NONE) {
    NEW_QUICK(%edx, 3);
  }
  CODE(opc_new_quick, new_quick, ST4, ST2, OPC_NONE) {
    asm("pushl %ecx");	// now state 1
    NEW_QUICK(%ecx, 1);
  }


  // nonnull_quick
  CODE(opc_nonnull_quick, nonnull_quick, ST0, ST0, OPC_THROW) {
    asm("popl  %edx");	// now state 1
    NULL_TEST(%edx, "nonnull_quick_st0_1");
  }
  CODE(opc_nonnull_quick, nonnull_quick, ST1, ST0, OPC_THROW) {
    NULL_TEST(%edx, "nonnull_quick_st1_1");
  }
  CODE(opc_nonnull_quick, nonnull_quick, ST2, ST1, OPC_THROW) {
    NULL_TEST(%ecx, "nonnull_quick_st2_1");
  }
  CODE(opc_nonnull_quick, nonnull_quick, ST3, ST0, OPC_THROW) {
    NULL_TEST(%ecx, "nonnull_quick_st3_1");
  }
  CODE(opc_nonnull_quick, nonnull_quick, ST4, ST3, OPC_THROW) {
    NULL_TEST(%edx, "nonnull_quick_st4_1");
  }


#ifdef SPECIAL_INLINING
  // inlined mathematical functions
#define JMATH_DIRECT_ST0(ROP) \
    asm("fldl  (%esp)\n\t" ROP "\n\tfstpl (%esp)")
#define JMATH_FUNCCALL_ST0(FUNC) \
    asm("call  " FUNCTION(FUNC) "\n\t"\
	"fstpl (%esp)")

#ifdef USE_SSE2
#  define JMATH_SQRT_ST0 \
    asm("movsd (%esp),%xmm0\n\t"\
	"sqrtsd %xmm0,%xmm0\n\t"\
	"movsd %xmm0,(%esp)");
#else
#  define JMATH_SQRT_ST0	JMATH_DIRECT_ST0("fsqrt")
#endif	// USE_SSE2
#if 0
#define JMATH_SIN_ST0	JMATH_DIRECT_ST0("fsin")
#define JMATH_COS_ST0	JMATH_DIRECT_ST0("fcos")
#define JMATH_TAN_ST0	JMATH_DIRECT_ST0("fptan\n\tffreep %st(0)")
#else
#define JMATH_SIN_ST0	JMATH_FUNCCALL_ST0(sin)
#define JMATH_COS_ST0	JMATH_FUNCCALL_ST0(cos)
#define JMATH_TAN_ST0	JMATH_FUNCCALL_ST0(tan)
#endif
#define JMATH_ATAN2_ST0 \
    asm("fldl  8(%esp)\n\tfldl  (%esp)\n\t"\
	"addl  $8,%esp\n\t"\
	"fpatan\n\t"\
	"fstpl (%esp)")
#define JMATH_ATAN_ST0	JMATH_DIRECT_ST0("fld1\n\tfpatan")
#define JMATH_LOG_ST0	JMATH_DIRECT_ST0("fldln2\n\tfxch %st(1)\n\tfyl2x")
#define JMATH_FLOOR_CEIL_ST0(ROP) \
    asm("fldl  (%esp)\n\t"\
	"subl  $4,%esp\n\t"\
	"fnstcw (%esp)\n\t"\
	"movw  (%esp),%ax\n\t"\
	ROP "\n\t"\
	"movw  %ax,2(%esp)\n\t"\
	"fldcw 2(%esp)\n\t"\
	"frndint\n\t"\
	"fldcw (%esp)\n\t"\
	"addl  $4,%esp\n\t"\
	"fstpl (%esp)")
#define JMATH_FLOOR_ST0 \
    JMATH_FLOOR_CEIL_ST0("andw $0xf3ff,%ax\n\torw $0x0400,%ax")
#define JMATH_CEIL_ST0 \
    JMATH_FLOOR_CEIL_ST0("andw $0xf3ff,%ax\n\torw $0x0800,%ax")
#if JDK_VER >= 12
#define JMATH_EXP_ST0 \
    asm("fldl  (%esp)\n\t"\
	"fldl2e\n\t"\
	"fmul  %st(1),%st\n\t"\
	"fst   %st(1)\n\t"\
	"frndint\n\t"\
	"fxch  %st(1)\n\t"\
	"fsub  %st(1),%st\n\t"\
	"f2xm1\n\t"\
	"fld1\n\t"\
	"faddp %st,%st(1)\n\t"\
	"fscale\n\t"\
	"fstpl (%esp)\n\t"\
	"ffreep %st(0)")
#define JMATH_ASIN_ST0 \
    asm("fldl  (%esp)\n\t"\
	"fld   %st(0)\n\t"\
	"fmul  %st(0)\n\t"	/* x^2 */\
	"fld1\n\t"\
	"fsubp\n\t"	/* 1 - x^2 */\
	"fsqrt\n\t"	/* sqrt(1 - x^2) */\
	"fpatan\n\t"	/* atan(x / sqrt(1 - x^2)) */\
	"fstpl (%esp)")
#define JMATH_ACOS_ST0 \
    asm("fldl  (%esp)\n\t"\
	"fld   %st(0)\n\t"\
	"fmul  %st(0)\n\t"	/* x^2 */\
	"fld1\n\t"\
	"fsubp\n\t"	/* 1 - x^2 */\
	"fsqrt\n\t"	/* sqrt(1 - x^2) */\
	"fxch  %st(1)\n\t"\
	"fpatan\n\t"	/* atan(sqrt(1 - x^2) / x) */\
	"fstpl (%esp)")
#endif	// JDK_VER >= 12

#define CODE_JMATH(vop, VOP) \
  CODE(opc_##vop, vop, ST0, ST0, OPC_NONE) {\
    JMATH_##VOP##_ST0;\
  }\
  CODE(opc_##vop, vop, ST1, ST0, OPC_NONE) {\
    asm("pushl %edx");	/* now state 0 */\
    JMATH_##VOP##_ST0;\
  }\
  CODE(opc_##vop, vop, ST2, ST0, OPC_NONE) {\
    asm("pushl %edx\n\t"\
	"pushl %ecx");	/* now state 0 */\
    JMATH_##VOP##_ST0;\
  }\
  CODE(opc_##vop, vop, ST3, ST0, OPC_NONE) {\
    asm("pushl %ecx");	/* now state 0 */\
    JMATH_##VOP##_ST0;\
  }\
  CODE(opc_##vop, vop, ST4, ST0, OPC_NONE) {\
    asm("pushl %ecx\n\t"\
	"pushl %edx");	/* now state 0 */\
    JMATH_##VOP##_ST0;\
  }

  CODE_JMATH(sqrt, SQRT);
  CODE_JMATH(sin, SIN);
  CODE_JMATH(cos, COS);
  CODE_JMATH(tan, TAN);
  CODE_JMATH(atan2, ATAN2);
  CODE_JMATH(atan, ATAN);
  CODE_JMATH(log, LOG);
  CODE_JMATH(floor, FLOOR);
  CODE_JMATH(ceil, CEIL);
#if JDK_VER >= 12
  CODE_JMATH(exp, EXP);
  CODE_JMATH(asin, ASIN);
  CODE_JMATH(acos, ACOS);
#endif	// JDK_VER >= 12

#define ABS_INT(REG) \
    asm("testl " #REG "," #REG "\n\t"\
	".byte 0x79,0x02\n\t"	/* jns */\
	"neg   " #REG)

  CODE(opc_abs_int, abs_int, ST0, ST1, OPC_NONE) {
    asm("movl  (%esp),%edx");
    ABS_INT(%edx);
  }
  CODE(opc_abs_int, abs_int, ST1, STSTA, OPC_NONE) {
    ABS_INT(%edx);
  }
  CODE(opc_abs_int, abs_int, ST2, STSTA, OPC_NONE) {
    ABS_INT(%ecx);
  }
  CODE(opc_abs_int, abs_int, ST3, STSTA, OPC_NONE) {
    ABS_INT(%ecx);
  }
  CODE(opc_abs_int, abs_int, ST4, STSTA, OPC_NONE) {
    ABS_INT(%edx);
  }

#define ABS_LONG_ST2 \
    asm("testl %edx,%edx\n\t"\
	".byte 0x79,0x7\n\t"	/* jns */\
	"neg   %ecx\n\t"\
	"adc   $0,%edx\n\t"\
	"neg   %edx")
#define ABS_LONG_ST4 \
    asm("testl %ecx,%ecx\n\t"\
	".byte 0x79,0x7\n\t"	/* jns */\
	"neg   %edx\n\t"\
	"adc   $0,%ecx\n\t"\
	"neg   %ecx")

  CODE(opc_abs_long, abs_long, ST0, ST2, OPC_NONE) {
    asm("popl  %ecx\n\t"
	"popl  %edx");	// now state 2
    ABS_LONG_ST2;
  }
  CODE(opc_abs_long, abs_long, ST1, ST4, OPC_NONE) {
    asm("popl  %ecx");	// now state 4
    ABS_LONG_ST4;
  }
  CODE(opc_abs_long, abs_long, ST2, ST2, OPC_NONE) {
    ABS_LONG_ST2;
  }
  CODE(opc_abs_long, abs_long, ST3, ST2, OPC_NONE) {
    asm("popl  %edx");	// now state 2
    ABS_LONG_ST2;
  }
  CODE(opc_abs_long, abs_long, ST4, ST4, OPC_NONE) {
    ABS_LONG_ST4;
  }

#define ABS_FLOAT(REG)	asm("andl  $0x7fffffff," #REG);

  CODE(opc_abs_float, abs_float, ST0, ST1, OPC_NONE) {
    asm("movl  (%esp),%edx");
    ABS_FLOAT(%edx);
  }
  CODE(opc_abs_float, abs_float, ST1, STSTA, OPC_NONE) {
    ABS_FLOAT(%edx);
  }
  CODE(opc_abs_float, abs_float, ST2, STSTA, OPC_NONE) {
    ABS_FLOAT(%ecx);
  }
  CODE(opc_abs_float, abs_float, ST3, STSTA, OPC_NONE) {
    ABS_FLOAT(%ecx);
  }
  CODE(opc_abs_float, abs_float, ST4, STSTA, OPC_NONE) {
    ABS_FLOAT(%edx);
  }

#define ABS_DOUBLE_ST2	asm("andl  $0x7fffffff,%edx");
#define ABS_DOUBLE_ST4	asm("andl  $0x7fffffff,%edx");

  CODE(opc_abs_double, abs_double, ST0, ST2, OPC_NONE) {
    asm("popl  %ecx\n\t"
	"popl  %edx");	// now state 2
    ABS_DOUBLE_ST2;
  }
  CODE(opc_abs_double, abs_double, ST1, ST4, OPC_NONE) {
    asm("popl  %ecx");	// now state 4
    ABS_DOUBLE_ST4;
  }
  CODE(opc_abs_double, abs_double, ST2, ST2, OPC_NONE) {
    ABS_DOUBLE_ST2;
  }
  CODE(opc_abs_double, abs_double, ST3, ST2, OPC_NONE) {
    asm("popl  %edx");	// now state 2
    ABS_DOUBLE_ST2;
  }
  CODE(opc_abs_double, abs_double, ST4, ST4, OPC_NONE) {
    ABS_DOUBLE_ST4;
  }


#if JDK_VER >= 12
#define CODE_ENSUREOPEN(HANDLE, LABEL) \
VALUE_DEBUG(HANDLE);\
    UNHAND(HANDLE, %eax);\
VALUE_DEBUG(%eax);\
    asm("movl  (%eax),%eax");	/* eax = unhand(HANLDLE)->in */\
VALUE_DEBUG(%eax);\
    asm("testl %eax,%eax\n\t"\
	"jnz   " LABEL "_done");\
    SIGNAL_ERROR1(EXCID_IOException, "Stream closed");\
    asm(LABEL "_done:");

  CODE(opc_java_io_bufferedinputstream_ensureopen, java_io_bufferedinputstream_ensureopen, ST0, ST0, OPC_THROW) {
    asm("popl  %edx");	// now state 1
    CODE_ENSUREOPEN(%edx, "ensureopen_st0");
  }
  CODE(opc_java_io_bufferedinputstream_ensureopen, java_io_bufferedinputstream_ensureopen, ST1, ST0, OPC_THROW) {
    CODE_ENSUREOPEN(%edx, "ensureopen_st1");
  }
  CODE(opc_java_io_bufferedinputstream_ensureopen, java_io_bufferedinputstream_ensureopen, ST2, ST1, OPC_THROW) {
    CODE_ENSUREOPEN(%ecx, "ensureopen_st2");
  }
  CODE(opc_java_io_bufferedinputstream_ensureopen, java_io_bufferedinputstream_ensureopen, ST3, ST0, OPC_THROW) {
    CODE_ENSUREOPEN(%ecx, "ensureopen_st3");
  }
  CODE(opc_java_io_bufferedinputstream_ensureopen, java_io_bufferedinputstream_ensureopen, ST4, ST3, OPC_THROW) {
    CODE_ENSUREOPEN(%edx, "ensureopen_st4");
  }
#endif	// JDK_VER >= 12
#endif	// SPECIAL_INLINING


  CODEEND;
}


//
// generated code jumps into this function directly.
//
void exceptionHandlerWrapper(
	/* arguments are the same as assembledCode() in code.c */
	JHandle *o /* 8(%ebp) */ , struct methodblock *mb /* 12(%ebp) */,
	int args_size, ExecEnv *ee, stack_item *var_base
#ifdef RUNTIME_DEBUG
	, int runtime_debug
#endif
) {
  // int32_t bytepcoff;		// -4(%ebp)
	// for handling exceptions

  DECL_GLOBAL_FUNC(SYMBOL(exceptionHandler));

#ifdef RUNTIME_DEBUG
  asm("pushl %eax");
  if (runtime_debug) {
    DEBUG_IN;
    PUSH_CONSTSTR("exceptionHandler called.\n");
    asm("call  " FUNCTION(printf) "\n\t"
	"addl  $4,%esp");
    FFLUSH;
    DEBUG_OUT;
  }
  asm("popl  %eax");
#endif

  // instantiate an exception

  // call SingalError() if it has not been called yet
  asm("movl  %0,%%edi" : : "m" (ee));
  asm("cmpb  $0," EE_EXCEPTIONKIND(%edi) "\n\t"
      "jnz   exc_new_done");
	// if (exceptionOccurred(ee))

  asm("pushl %edx");		// edx should be (char *) DetailMessage

  asm("movl  %0,%%edi" : : "D" (signal_name) : "eax","esi","edx","ecx");
	// "m" is desirable but gcc 4.0 does not allow

  asm("andl  $0xff,%eax");	// al contains EXCID
#ifdef RUNTIME_DEBUG
  asm("pushl %eax");
  if (runtime_debug) {		// break eax
    asm("movl  (%esp),%eax");	// restore
    DEBUG_IN;
    asm("pushl %eax");
    PUSH_CONSTSTR("exc ID: %d\n");
    asm("call  " FUNCTION(printf) "\n\t"
	"addl  $8,%esp");
    FFLUSH;
    DEBUG_OUT;
  }
  asm("popl  %eax");
#endif
  asm("movl  (%edi,%eax,4),%eax");
	// eax = signal_name[eax]
  asm("pushl %%eax\n\t"		// eax should be (char *) ename
      "pushl %0\n\t"
      "call  " FUNCTION(SignalError) "\n\t"
      "addl  $12,%%esp" : : "m" (ee));
  asm("exc_new_done:");


#if 0 && defined(METAVM)
  // clear remote flag
  asm("movl  %0,%%edi\n\t"
      "movb  $0," EE_REMOTE_FLAG(%%edi)
      : : "m" (ee));
#endif

  // call searchCatchFrame()

#ifdef RUNTIME_DEBUG
  asm("pushl %0" : : "m" (runtime_debug));
#endif	// RUNTIME_DEBUG
  asm("pushl -4(%%ebp)\n\t"	// bytepcoff
      "pushl %0\n\t"
      "pushl %1"
      : : "m" (mb), "m" (ee));
  asm("call  " FUNCTION(searchCatchFrame));
#ifdef RUNTIME_DEBUG
  asm("addl  $16,%esp");
#else
  asm("addl  $12,%esp");
#endif	// RUNTIME_DEBUG
	// eax is CatchFrame

  asm("testl %eax,%eax\n\t"
      "jnz   exc_caught\n\t"
      "ret\n\t"
    "exc_caught:");

  // clear the stack
  asm("leal  -" STR(LOCAL_VAR_AREA) "-" STR(SAVED_REG_AREA) "(%ebp),%esp");

  {	// place the exception object on TOS
    asm("movl  %0,%%edi" : : "m" (ee));
    asm("pushl " EE_EXCEPTION(%edi));

    asm("movl  $0," EE_EXCEPTION(%edi));
  }

#ifdef METHOD_INLINING
  // esi = var_base:  have to be re-initialized
  asm("movl  24(%ebp),%esi");
#endif

  {
    register struct CatchFrame_w_state *cf asm("eax");
    register int32_t handler_state asm("ecx");

    asm("movswl " CATCHFRAME_COMPILED_STATE(%eax) ",%ecx");
	// handler_state = cf->state;
#ifdef RUNTIME_DEBUG
    asm("pushl %eax");
    if (runtime_debug) {
      DEBUG_IN;
      asm("pushl %ecx");
      PUSH_CONSTSTR("  exc handler state: %d\n");
      asm("call  " FUNCTION(printf) "\n\t"
	"addl  $8,%esp");
      FFLUSH;
      DEBUG_OUT;
    }
    asm("popl  %eax");
#endif

    // shift to the state which handler assumes
    asm("cmpl  $0,%ecx\n\t"	// if (hanlder_state == 0)
	"je   exc_handler_shift_done\n\t"
      "exc_handler_shift_1:\n\t"
	"cmpl  $1,%ecx\n\t"	// if (handler_state == 1)
	"jne   exc_handler_shift_2\n\t"
	"popl  %edx");
#ifdef RUNTIME_DEBUG
    asm("pushl %eax");
    if (runtime_debug) {
      DEBUG_IN;
      asm("pushl %edx");
      PUSH_CONSTSTR("  st1 edx: %x\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $8,%esp");
      DEBUG_OUT;
    }
    asm("popl  %eax");
#endif
    asm("jmp   exc_handler_shift_done\n\t"
      "exc_handler_shift_2:\n\t"
	"cmpl  $3,%ecx\n\t"	// if (handler_state == 3)
	"jne   exc_handler_shift_3\n\t"
	"popl  %ecx");
#ifdef RUNTIME_DEBUG
    asm("pushl %eax");
    if (runtime_debug) {
      DEBUG_IN;
      asm("pushl %ecx");
      PUSH_CONSTSTR("  st3 ecx: %x\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $8,%esp");
      FFLUSH;
      DEBUG_OUT;
    }
    asm("popl  %eax");
#endif
    asm("jmp   exc_handler_shift_done\n\t"
      "exc_handler_shift_3:\n\t"
	"cmpl  $2,%ecx\n\t"	// if (handler_stat == 2)
	"jne   exc_handler_shift_4\n\t"
	"popl  %ecx\n\t"
	"xorl  %edx,%edx");
#ifdef RUNTIME_DEBUG
    asm("pushl %eax");
    if (runtime_debug) {
      DEBUG_IN;
      asm("pushl %ecx");
      PUSH_CONSTSTR("  st2 ecx: %x\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $8,%esp");
      FFLUSH;
      DEBUG_OUT;
    }
    asm("popl  %eax");
#endif
    asm("jmp   exc_handler_shift_done\n\t"
      "exc_handler_shift_4:\n\t"
	"cmpl  $4,%ecx\n\t"	// if (handler_stat == 4)
	"jne   exc_handler_shift_done\n\t"
	"popl  %edx\n\t"
	"xorl  %ecx,%ecx");
#ifdef RUNTIME_DEBUG
    asm("pushl %eax");
    if (runtime_debug) {
      DEBUG_IN;
      asm("pushl %edx");
      PUSH_CONSTSTR("  st4 edx: %x\n");
      asm("call  " FUNCTION(printf) "\n\t"
	  "addl  $8,%esp");
      FFLUSH;
      DEBUG_OUT;
    }
    asm("popl  %eax");
#endif
  asm("exc_handler_shift_done:");

    COMPILEDCODE(%edi);		// edi = mb->CompiledCode
    asm("addl  " CATCHFRAME_COMPILED_CATCHFRAME(%eax) ",%edi");
	// edi += (int)cf->compiled_CatchFrame;
    asm("jmp   *%edi");
  }
}
