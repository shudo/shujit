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

#include "config.h"

/*
 * ESI: vars
 * EDX: cache of stack top
 * ECX: cache of stack top
 */

/*
 * 5 states of Java stack
 *
 * state 0:
 *   ECX: undefined
 *   EDX: undefined
 * state 1:
 *   ECX: undefined
 *   EDX: stack top element
 * state 2:
 *   ECX: stack top element
 *   EDX: stack top-1 element
 * state 3:
 *   EDX: undefined
 *   ECX: stack top element
 * state 4:
 *   EDX: stack top element
 *   ECX: stack top-1 element
 */

#define	ST0	0
#define	ST1	1
#define	ST2	2
#define	ST3	3
#define	ST4	4
#define STANY	5
#define STSTA	5

#define NSTATES	5

// state for next instruction of goto,jsr,ret
#define STATE_AFTER_JUMP	ST0
#define STATE_AFTER_JSR		ST0
#define STATE_AFTER_RETURN	ST0
#define STATE_AFTER_ATHROW	ST0
	// should be `ST0' because we cannot expect
	// the stack has elements enough for other states.


//
// space filled by compiler
//

// constant
#define SLOT_CONST	0x606060
// for exception handler
#define SLOT_BYTEPCOFF	0x626262
#define SLOT_ADDR_EXC	0x707010
// for jump instructions
#define SLOT_ADDR_JP		0x727210
// for *return instructions
#define SLOT_ADDR_FIN	0x747410


//
// utilities
//

#if GCC_VER <= 270
#define PUSH_CONSTSTR(STR)	asm("PUSH %0" : : "m" (STR))
#else
#define PUSH_CONSTSTR(STR) \
  {\
    static char *msg = STR;\
    asm("pushl %0" : : "m" (msg) : "esi");\
  }
#endif
	// This code must not use %esi which is the base of local variables

#if defined(linux)
#  if !defined(__GLIBC__) || ((__GLIBC__ == 2) && (__GLIBC_MINOR__ == 0))
#    define PUSH_STDOUT \
  asm("movl  _IO_stdout_@GOT(%ebx),%eax\n\t"\
      "pushl %eax");
#  else
#    define PUSH_STDOUT \
  asm("movl  stdout@GOT(%ebx),%eax\n\t"\
      "pushl (%eax)");
#  endif	// __GLIBC__
#elif defined(__FreeBSD__) || defined(__NetBSD__)
#  define PUSH_STDOUT \
  asm("movl  " SYMBOL(__sF) "@GOT(%ebx),%eax\n\t"\
      "leal  88(%eax),%eax\n\t"\
      "pushl %eax");
#elif defined(sun) && defined(__svr4__)
#  define PUSH_STDOUT \
  asm("movl  __iob@GOT(%ebx),%eax\n\t"\
      "leal  16(%eax),%eax\n\t"\
      "pushl %eax");
#endif

#define FFLUSH \
  PUSH_STDOUT;\
  asm("call  " FUNCTION(fflush) "\n\t"\
      "addl  $4,%esp")

#if 0
#define DEBUG_IN \
  asm("pushl %eax\n\tpushl %edx\n\tpushl %ebx\n\tpushl %ecx\n\t"\
      "pushl %edi\n\tpushl %esi")
#define DEBUG_OUT \
  asm("popl  %esi\n\tpopl  %edi\n\t"\
      "popl  %ecx\n\tpopl  %ebx\n\tpopl  %edx\n\tpopl  %eax")
#else
#define DEBUG_IN	asm("pusha")
#define DEBUG_OUT	asm("popa")
#endif

#define FUNCCALL_IN(STATE)	_FUNCCALL_IN(STATE)
#define FUNCCALL_OUT(STATE)	_FUNCCALL_OUT(STATE)
#define _FUNCCALL_IN(STATE)	FUNCCALL_IN_##STATE
#define _FUNCCALL_OUT(STATE)	FUNCCALL_OUT_##STATE
#define FUNCCALL_IN_0
#define FUNCCALL_OUT_0
#define FUNCCALL_IN_1	asm("pushl %edx")
#define FUNCCALL_OUT_1	asm("popl  %edx")
#define FUNCCALL_IN_2	asm("pushl %edx\n\tpushl %ecx")
#define FUNCCALL_OUT_2	asm("popl  %ecx\n\tpopl  %edx")
#define FUNCCALL_IN_3	asm("pushl %ecx")
#define FUNCCALL_OUT_3	asm("popl  %ecx")
#define FUNCCALL_IN_4	asm("pushl %ecx\n\tpushl %edx")
#define FUNCCALL_OUT_4	asm("popl  %edx\n\tpopl  %ecx")


#define STATETO00
#define STATETO10	asm("pushl %edx")
#define STATETO20	asm("pushl %edx\n\tpushl %ecx")
#define STATETO30	asm("pushl %ecx")
#define STATETO40	asm("pushl %ecx\n\tpushl %edx")


#ifndef _WIN32
#  define DECL_GLOBAL_FUNC(FUNCNAME_STR) \
  asm(".globl " FUNCNAME_STR "\n"\
      ".type " FUNCNAME_STR ",@function\n"\
      FUNCNAME_STR ":");
#else
#  define DECL_GLOBAL_FUNC(FUNCNAME_STR) \
  asm(".globl " FUNCNAME_STR "\n"\
      ".def " FUNCNAME_STR ";\n\t"\
      ".scl 2;\n\t"\
      ".type 32;\n"\
      ".enddef\n"\
      FUNCNAME_STR ":");
#endif


#define SIGNAL_ERROR_JUMP() \
  asm("movl  $" STR(SLOT_BYTEPCOFF) ",-4(%ebp)");\
	/* bytepcoff = SLOT_BYTEPCOFF */\
  asm(".byte 0xe9\n\t.long " STR(SLOT_ADDR_EXC))

#define SIGNAL_ERROR_CORE(EXCID) \
  asm("movb  $" #EXCID ",%al");\
  SIGNAL_ERROR_JUMP()

#define SIGNAL_ERROR0(EXCID) \
  asm("xorl  %edx,%edx");			/* char *DetailMessage */\
  SIGNAL_ERROR_CORE(EXCID)

#define SIGNAL_ERROR1(EXCID, MSG) \
  {\
    static char *errmsg = MSG;\
    asm("movl  %0,%%edx" : : "m" (errmsg)); /* char *DetailMessage */\
    SIGNAL_ERROR_CORE(EXCID);\
  }



#if !defined(NO_NULL_AND_ARRAY_CHECK) && !defined(NULLEXC_BY_SIGNAL)
#  define NULL_TEST(REG, LABEL) \
  asm("testl " #REG "," #REG "\n\t"\
      "jnz   " LABEL);\
  SIGNAL_ERROR0(EXCID_NullPointerException);\
  asm(LABEL ":")
#else
#  define NULL_TEST(REG, LABEL)
#endif


/*
 * code header
 *   for each opcode of JVM.
 *
 * 0xd6 (0x82, 0xd6, 0xf1 is not defined in x86 insn. set)
 * 0xe9, opcode(2 byte), initial state(4 bit), last state(4 bit)
 */

#ifdef RUNTIME_DEBUG
#define CODE(OPCODE, LABEL, STATE, NEXTST, THROW_EXC) \
  CODE_WITH_DEBUG(OPCODE, LABEL, STATE, NEXTST, THROW_EXC)
#else
#define CODE(OPCODE, LABEL, STATE, NEXTST, THROW_EXC) \
  CODE_WITHOUT_DEBUG(OPCODE, LABEL, STATE, NEXTST, THROW_EXC)
#endif

#ifdef RUNTIME_DEBUG
#  define CODE_DEBUG(LABEL) /* print %esp */\
  asm("pushl %eax\n\t"\
      "leal  4(%esp),%eax");\
  DEBUG_IN;\
  asm("pushl %eax");\
  if (runtime_debug) {\
    PUSH_CONSTSTR(">" LABEL " %x\n");\
    asm("call " FUNCTION(printf) "\n\t"\
	"addl $4,%esp");\
    FFLUSH;\
  }\
  asm("addl $4,%esp");\
  DEBUG_OUT;\
  asm("popl  %eax")
#else
#  define CODE_DEBUG(LABEL)
#endif	// RUNTIME_DEBUG

#define	CODE_WITH_DEBUG(OPCODE, LABEL, STATE, NEXTST, THROW_EXC) \
  _CODE_WITH_DEBUG(OPCODE, LABEL, STATE, NEXTST, THROW_EXC)
#define	_CODE_WITH_DEBUG(OPCODE, LABEL, STATE, NEXTST, THROW_EXC) \
  CODE_WITHOUT_DEBUG(OPCODE, LABEL, STATE, NEXTST, THROW_EXC);\
  CODE_DEBUG(#LABEL ":" #STATE ">" #NEXTST );
#define	CODE_WITHOUT_DEBUG(OPCODE, LABEL, STATE, NEXTST, THROW_EXC) \
  _CODE_WITHOUT_DEBUG(OPCODE, LABEL, STATE, NEXTST, THROW_EXC)
#define	_CODE_WITHOUT_DEBUG(OPCODE, LABEL, STATE, NEXTST, THROW_EXC) \
  asm(".byte 0xd6\n\t"\
      ".byte 0xe9\n\t"\
      ".short " #OPCODE "\n\t"\
      ".byte (" #STATE "<<4)|" #NEXTST "," #THROW_EXC);

#define CODEEND	asm(".byte 0xd6,0xd6")


//
// JDK related stuffs
//

#define UNHAND(HANDLE, DST)	asm("movl  (" #HANDLE ")," #DST)

#define OBJ_MONITOR(HANDLE)
#define OBJ_LENGTH(HANDLE, DST) \
  asm("movl  4(" #HANDLE ")," #DST "\n\t"\
      "shrl  $5," #DST)
#define OBJ_METHODTABLE(HANDLE, DST) \
  asm("movl  4(" #HANDLE ")," #DST)
#define OBJ_ARRAY_METHODTABLE(HANDLE, DST, LABEL) \
	/* assumption: T_NORMAL_OBJECT == 0 */\
  asm("movl  4(" #HANDLE ")," #DST "\n\t"\
      "testl $0x1f," #DST "\n\t"\
      "jz    " LABEL "_mtdone");\
  asm("movl  classJavaLangObject@GOT(%ebx),%edi\n\t"\
      "movl  (%edi)," #DST);\
  CB_METHODTABLE(DST, DST);\
  asm(LABEL "_mtdone:")
	// destroy %edi, DST can't be %edi
#define OBJ_ARRAY_METHODTABLE_TO_EAX(HANDLE, LABEL) \
	/* assumption: T_NORMAL_OBJECT == 0 */\
  asm("movl  4(" #HANDLE "),%eax\n\t"\
      "testb $0x1f,%al\n\t"\
      "jz    " LABEL "_mtdone");\
  asm("movl  classJavaLangObject@GOT(%ebx),%edi\n\t"\
      "movl  (%edi),%eax");\
  CB_METHODTABLE(%eax, %eax);\
  asm(LABEL "_mtdone:")

#define MT_SLOT(MTBL, SLOT, DST) \
  asm("movl  4(" #MTBL "," #SLOT ",4)," #DST)
#define MT_CLASSDESCRIPTOR(MTBL, DST) \
  asm("movl  (" #MTBL ")," #DST)

#define UOBJ_GETSLOT(OBJ, SLOT, DST) \
  asm("movl  (" #OBJ "," #SLOT ",4)," #DST)

#define OBJ_GETSLOT(HANDLE, SLOT, DST) \
  UNHAND(HANDLE, %edi);\
  UOBJ_GETSLOT(%edi, SLOT, DST)

#define UOBJ_GETSLOT2(OBJ, SLOT, DST_LOW, DST_HIGH) \
  asm("leal  (" #OBJ "," #SLOT ",4),%edi\n\t"\
      "movl  (%edi)," #DST_LOW "\n\t"\
      "movl  4(%edi)," #DST_HIGH)

#define OBJ_GETSLOT2(HANDLE, SLOT, DST_LOW, DST_HIGH) \
  UNHAND(HANDLE, %edi);\
  UOBJ_GETSLOT2(%edi, SLOT, DST_LOW, DST_HIGH)

#define UOBJ_SETSLOT(OBJ, SLOT, VAL) \
  asm("movl  " #VAL ",(" #OBJ "," #SLOT ",4)")

#define OBJ_SETSLOT(HANDLE, SLOT, VAL) \
  UNHAND(HANDLE, %edi);\
  UOBJ_SETSLOT(%edi, SLOT, VAL)

#define UOBJ_SETSLOT2(OBJ, SLOT, VAL_LOW, VAL_HIGH) \
  asm("leal  (" #OBJ "," #SLOT ",4),%edi\n\t"\
      "movl  " #VAL_LOW ",(%edi)\n\t"\
      "movl  " #VAL_HIGH ",4(%edi)")

#define OBJ_SETSLOT2(HANDLE, SLOT, VAL_LOW, VAL_HIGH) \
  UNHAND(HANDLE, %edi);\
  UOBJ_SETSLOT2(%edi, SLOT, VAL_LOW, VAL_HIGH)

#define CB_NAME(CLAZZ, DST) \
  asm("movl  (" #CLAZZ "),%edi\n\t"\
      "movl  4(%edi)," #DST)
#define CB_LOADER(CLAZZ, DST) \
  asm("movl  (" #CLAZZ "),%edi\n\t"\
      "movl  24(%edi)," #DST)
#if JDK_VER >= 12
#define CB_ACCESS(CLAZZ, DST) \
  asm("movl  (" #CLAZZ "),%edi\n\t"\
      "movzwl 84(%edi)," #DST)
#else
#define CB_ACCESS(CLAZZ, DST) \
  asm("movl  (" #CLAZZ "),%edi\n\t"\
      "movzwl 86(%edi)," #DST)
#endif	// JDK_VER
#define CB_METHODTABLE(CLAZZ, DST) \
  asm("movl  (" #CLAZZ "),%edi\n\t"\
      "movl  48(%edi)," #DST)

#if JDK_VER >= 12
#define EE_CURRENTFRAME(EE)	"8(" #EE ")"
#define EE_EXCEPTIONKIND(EE)	"16(" #EE ")"
#define EE_EXCEPTION(EE)	"20(" #EE ")"
#else
#define EE_CURRENTFRAME(EE)	"4(" #EE ")"
#define EE_EXCEPTIONKIND(EE)	"12(" #EE ")"
#define EE_EXCEPTION(EE)	"16(" #EE ")"
#endif	// JDK_VER


#if JDK_VER >= 12
#  define ALLOC_ARRAY(TYPE, LEN) \
    asm("pushl " #LEN "\n\t"\
	"pushl " TYPE);\
    asm("pushl %0" : : "m" (ee));\
    asm("call  " FUNCTION(allocArray) "\n\t"\
	"addl  $12,%esp");
#else
#  define ALLOC_ARRAY(TYPE, LEN) \
    asm("pushl " #LEN "\n\t"\
	"pushl " TYPE "\n\t"\
	"call  " FUNCTION(ArrayAlloc) "\n\t"\
	"addl  $8,%esp");
#endif


#ifdef METAVM
#  define EE_EXCEPTIONKIND_EAX(EE) \
  asm("movsbl " EE_EXCEPTIONKIND(EE) ",%eax")
#else
#  define EE_EXCEPTIONKIND_EAX(EE) \
  asm("movl " EE_EXCEPTIONKIND(EE) ",%eax")
	// This is rather desirable, but not compatible with MetaVM.
#endif	// METAVM

#define FRAME_RETURNPC(FRAME)	"4(" #FRAME ")"
#define FRAME_OPTOP(FRAME)	"8(" #FRAME ")"
#define FRAME_VARS(FRAME)	"12(" #FRAME ")"
// override previous definition in interpreter.h of JDK 1.2
#ifdef FRAME_PREV
#undef FRAME_PREV
#endif
#define FRAME_PREV(FRAME)	"16(" #FRAME ")"
#define FRAME_LASTPC(FRAME)	"24(" #FRAME ")"
#define FRAME_CURRENTMETHOD(FRAME)	"28(" #FRAME ")"
#define FRAME_MONITOR(FRAME)	"32(" #FRAME ")"
#define FRAME_OSTACK(FRAME)	"40(" #FRAME ")"

#define METHOD_CLAZZ(MB)	"(" #MB ")"
#define METHOD_SIGNATURE(MB)	"4(" #MB ")"
#define METHOD_NAME(MB)		"8(" #MB ")"
#define METHOD_CODE(MB)		"24(" #MB ")"
#if JDK_VER >= 12
#define METHOD_ACCESS(MB)	"12(" #MB ")"
#define METHOD_ACCESS_HIGH(MB)	"13(" #MB ")"
#define METHOD_FB_U_OFFSET(MB)	"16(" #MB ")"
#define METHOD_INVOKER(MB)	"52(" #MB ")"
#define METHOD_NLOCALS(MB, DST)	asm("movzwl 60(" #MB ")," #DST)
#else
#define METHOD_ACCESS(MB)	"16(" #MB ")"
#define METHOD_ACCESS_HIGH(MB)	"17(" #MB ")"
#define METHOD_FB_U_OFFSET(MB)	"20(" #MB ")"
#define METHOD_INVOKER(MB)	"56(" #MB ")"
#define METHOD_NLOCALS(MB, DST)	asm("movzwl 64(" #MB ")," #DST)
#endif	// JDK_VER
#define METHOD_COMPILEDCODE(MB)	"68(" #MB ")"
#define METHOD_COMPILEDCODEINFO(MB)	"72(" #MB ")"

#if JDK_VER >= 12
#define CATCHFRAME_COMPILED_CATCHFRAME(CF)	"8(" #CF ")"
#define CATCHFRAME_COMPILED_STATE(CF)		"14(" #CF ")"
#else
#define CATCHFRAME_COMPILED_CATCHFRAME(CF)	"12(" #CF ")"
#define CATCHFRAME_COMPILED_STATE(CF)		"18(" #CF ")"
#endif	// JDK_VER


#ifdef METAVM
//
// MetaVM related stuff
//
#if JDK_VER >= 12
#define EE_REMOTE_FLAG(EE)	"17(" #EE ")"
	// ((char *)ee->exceptionKind) + 1
#  define EE_REMOTE_ADDR(EE)	"120(" #EE ")"
	// ee->RESERVED3
//#  define EE_REMOTE_ADDR(EE)	"20(" #EE ")"
	// ee->exception.exc
#else	// JDK_VER
#define EE_REMOTE_FLAG(EE)	"61(" #EE ")"
	// ee->alloc_cache.cache_pad[0]
#define EE_REMOTE_ADDR(EE)	"16(" #EE ")"
	// ee->exception.exc
#endif	// JDK_VER

#define JUMP_IF_NOT_REMOTE(LABEL) \
    asm("movl  %0,%%edi" : : "m" (ee));\
    asm("movsbl " EE_REMOTE_FLAG(%edi) ",%edi");\
    asm("testl %edi,%edi\n\t"\
	"jz    " LABEL)
	// break %edi

#define METHODTABLE_OF_PROXY(DST) \
    asm("movl  " SYMBOL(proxy_methodtable) "@GOT(%ebx)," #DST "\n\t"\
	"movl  (" #DST ")," #DST)
	// break no registers

#define JUMP_IF_NOT_PROXY(HANDLE_REG /* must not be %edi */, LABEL) \
    METHODTABLE_OF_PROXY(%edi);\
    asm("cmpl  4(" #HANDLE_REG "),%edi\n\t"\
	"jnz   " LABEL)
	// break %edi

#define PROXY_CLAZZ(HANDLE, DST) \
    UNHAND(HANDLE, DST);\
    asm("movl  8(" #DST ")," #DST);
	// this constant depends on the definition of `Proxy' class !

#define JUMP_IF_EXC_HASNT_OCCURRED(EE, LABEL) \
  asm("movsbl " EE_EXCEPTIONKIND(EE) ",%edi");\
  asm("testl %edi,%edi\n\t"\
      "jz    " LABEL)
#else
#define JUMP_IF_NOT_REMOTE(LABEL)
#define JUMP_IF_NOT_PROXY(HANDLE_REG, LABEL)
#endif	// METAVM
