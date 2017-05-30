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

#include <string.h>	// for strlen(), memcpy()
#include <stdarg.h>	// for va_*()

#include "metavm.h"
#include "native.h"
#if 0
#include "NET_shudo_metavm_Proxy.h"
#endif
#include "NET_shudo_metavm_Proxy_old.h"
#include "NET_shudo_metavm_VMAddress_old.h"

#include "java_lang_Boolean_old.h"
#include "java_lang_Byte_old.h"
#include "java_lang_Character_old.h"
#include "java_lang_Short_old.h"
#include "java_lang_Integer_old.h"
#include "java_lang_Long_old.h"
#include "java_lang_Float_old.h"
#include "java_lang_Double_old.h"

#define PROXY_CLASSNAME	METAVM_PKG "Proxy"


// Global Varialbe
struct methodtable *proxy_methodtable;


ClassClass *cb_Boolean = NULL;
ClassClass *cb_Byte = NULL;
ClassClass *cb_Character = NULL;
ClassClass *cb_Short = NULL;
ClassClass *cb_Integer = NULL;
ClassClass *cb_Long = NULL;
ClassClass *cb_Float = NULL;
ClassClass *cb_Double = NULL;

ClassClass *cb_ArrayOfObject = NULL;

ClassClass *cb_Proxy = NULL;

#define MB_DECL(METHOD_NAME) \
  static struct methodblock *mb_##METHOD_NAME = NULL
MB_DECL(get32field);
MB_DECL(get64field);
MB_DECL(getobjfield);
MB_DECL(aload32);
MB_DECL(aload64);
MB_DECL(aloadobj);
MB_DECL(put32field);
MB_DECL(put64field);
MB_DECL(putobjfield);
MB_DECL(astore32);
MB_DECL(astore64);
MB_DECL(astoreobj);
MB_DECL(getObjCopy);
MB_DECL(getArrayCopy);
MB_DECL(aryBlockLoad);
MB_DECL(aryBlockStore);


JNIEXPORT void JNICALL Java_NET_shudo_metavm_Proxy_initNative
  (JNIEnv *env, jclass clazz) {
  ExecEnv *ee = JNIEnv2EE(env);
  ClassClass *cb = (ClassClass *)DeRef(env, clazz);
#if 0
printf("cb of Proxy: 0x%08x\n", (int)cb);
#endif

#define CLAZZ_INIT(JCLASS, CLASSNAME) \
  cb_##JCLASS = FindClass(ee, CLASSNAME, TRUE)
#define LANG_CLAZZ_INIT(JCLASS) \
  CLAZZ_INIT(JCLASS, "java/lang/" #JCLASS)

  LANG_CLAZZ_INIT(Boolean);
  LANG_CLAZZ_INIT(Byte);
  LANG_CLAZZ_INIT(Character);
  LANG_CLAZZ_INIT(Short);
  LANG_CLAZZ_INIT(Integer);
  LANG_CLAZZ_INIT(Float);
  LANG_CLAZZ_INIT(Long);
  LANG_CLAZZ_INIT(Double);

  CLAZZ_INIT(ArrayOfObject, "[Ljava/lang/Object;");
  CLAZZ_INIT(Proxy, PROXY_CLASSNAME);
#if JDK_VER < 12
  // prevent class unloading
  (*env)->NewGlobalRef(env, MK_REF_LOCAL(env, (JHandle *)cb_ArrayOfObject));
  (*env)->NewGlobalRef(env, MK_REF_LOCAL(env, (JHandle *)cb_Proxy));
#endif	// JDK_VER

  proxy_methodtable = unhand(cb)->methodtable;
#if 0
printf("proxy_methodtable: 0x%08x\n", (int)proxy_methodtable);
fflush(stdout);
#endif

  // initialize methodblock
  {
    int i;
    struct methodblock *mb;

#if JDK_VER >= 12
    HashedNameAndType
#else
    unsigned 
#endif
      hashed_get32field, hashed_get64field, hashed_getobjfield,
      hashed_aload32, hashed_aload64, hashed_aloadobj,
      hashed_put32field, hashed_put64field, hashed_putobjfield,
      hashed_astore32, hashed_astore64, hashed_astoreobj,
      hashed_getObjCopy, hashed_getArrayCopy,
      hashed_aryBlockLoad, hashed_aryBlockStore;

#if JDK_VER >= 12
#  define HASH_METHOD(NAME, SIG) \
    HashNameAndType(ee, #NAME, SIG, &hashed_##NAME)
#else
#  define HASH_METHOD(NAME, SIG) \
    hashed_##NAME = NameAndTypeToHash(#NAME, SIG)
#endif

    HASH_METHOD(get32field, "(I)I");
    HASH_METHOD(get64field, "(I)J");
    HASH_METHOD(getobjfield, "(I)Ljava/lang/Object;");
    HASH_METHOD(aload32, "(I)I");
    HASH_METHOD(aload64, "(I)J");
    HASH_METHOD(aloadobj, "(I)Ljava/lang/Object;");
    HASH_METHOD(put32field, "(II)V");
    HASH_METHOD(put64field, "(IJ)V");
    HASH_METHOD(putobjfield, "(ILjava/lang/Object;)V");
    HASH_METHOD(astore32, "(II)V");
    HASH_METHOD(astore64, "(IJ)V");
    HASH_METHOD(astoreobj, "(ILjava/lang/Object;)V");
    HASH_METHOD(getObjCopy, "()Ljava/lang/Object;");
    HASH_METHOD(getArrayCopy, "(II)Ljava/lang/Object;");
    HASH_METHOD(aryBlockLoad, "(Ljava/lang/Object;III)V");
    HASH_METHOD(aryBlockStore, "(Ljava/lang/Object;III)V");

    for (i = cbMethodsCount(cb_Proxy) - 1; i >= 0; i--) {
      mb = &(cbMethods(cb_Proxy)[i]);
#if JDK_VER >= 12
#  define IF_METHOD(NAME) \
      if (NAMETYPE_MATCH(&hashed_##NAME, &(mb->fb)))\
	mb_##NAME = mb
#else
#  define IF_METHOD(NAME) \
      if (mb->fb.ID == hashed_##NAME)  mb_##NAME = mb
#endif
#define ELIF_METHOD(NAME)	else IF_METHOD(NAME)

      IF_METHOD(get32field);
      ELIF_METHOD(get64field);
      ELIF_METHOD(getobjfield);
      ELIF_METHOD(aload32);
      ELIF_METHOD(aload64);
      ELIF_METHOD(aloadobj);
      ELIF_METHOD(put32field);
      ELIF_METHOD(put64field);
      ELIF_METHOD(putobjfield);
      ELIF_METHOD(astore32);
      ELIF_METHOD(astore64);
      ELIF_METHOD(astoreobj);
      ELIF_METHOD(getObjCopy);
      ELIF_METHOD(getArrayCopy);
      ELIF_METHOD(aryBlockLoad);
      ELIF_METHOD(aryBlockStore);
    }

    sysAssert((mb_get32field != NULL) && (mb_get64field) &&
		(mb_getobjfield != NULL) && (mb_aload32) &&
		(mb_aload64 != NULL) && (mb_aloadobj) &&
		(mb_put32field != NULL) && (mb_put64field) &&
		(mb_putobjfield != NULL) && (mb_astore32) &&
		(mb_astore64 != NULL) && (mb_astoreobj) &&
		(mb_getObjCopy != NULL) && (mb_getArrayCopy != NULL) &&
		(mb_aryBlockLoad != NULL) && (mb_aryBlockStore != NULL));
  }
}


JHandle *proxy_new(ExecEnv *ee, ClassClass *fromClazz,
	HNET_shudo_metavm_VMAddress *addr, ClassClass *cb) {
  JHandle *obj;

  char orig = GET_REMOTE_FLAG(ee);
  REMOTE_FLAG_OFF(ee);
#ifdef RUNTIME_DEBUG
  printf("proxy_new(0x%08x, 0x%08x, %s(0x%08x)) called.\n",
	(int)ee, (int)addr, (cb?cbName(cb):"null"), (int)cb);
  fflush(stdout);
  printf("  port no: %d\n", unhand(addr)->port);
  fflush(stdout);
#endif

  sysAssert(cb_Proxy != NULL);

  obj = (JHandle *)do_execute_java_method(ee, cb_Proxy,
	"get",
	"(Ljava/lang/Class;L" METAVM_PKG "VMAddress;Ljava/lang/Class;)L" METAVM_PKG "Proxy;",
	NULL,	// struct methodblock
	TRUE,	// isStaticCall
	fromClazz, addr, cb);
#ifdef RUNTIME_DEBUG
  if (exceptionOccurred(ee)) {
    printf("exc. occurred.\n");
    fflush(stdout);
  }
#endif

  SET_REMOTE_FLAG(ee, orig);

#ifdef RUNTIME_DEBUG
  printf("proxy_new() done. obj: 0x%08x\n", (int)obj);
  fflush(stdout);
#endif
  return obj;
}


JHandle *proxy_newarray(ExecEnv *ee, ClassClass *fromClazz,
	HNET_shudo_metavm_VMAddress *addr, int type, int count) {
  JHandle *obj;

  char orig = GET_REMOTE_FLAG(ee);
  REMOTE_FLAG_OFF(ee);
#ifdef RUNTIME_DEBUG
  printf("proxy_newarray() type, count: %d, %d\n", type, count);
  fflush(stdout);
#endif

  sysAssert(cb_Proxy != NULL);

  obj = (JHandle *)do_execute_java_method(ee, cb_Proxy,
	"get",
	"(Ljava/lang/Class;L" METAVM_PKG "VMAddress;II)L" METAVM_PKG "Proxy;",
	NULL,	// struct methodblock
	TRUE,	// isStaticCall
	fromClazz, addr, type, count);
#ifdef RUNTIME_DEBUG
  if (exceptionOccurred(ee)) {
    printf("exc. occurred.\n");
    fflush(stdout);
  }
#endif

  SET_REMOTE_FLAG(ee, orig);

#ifdef RUNTIME_DEBUG
  printf("proxy_newarray() done. obj: 0x%08x\n", (int)obj);
  fflush(stdout);
#endif
  return obj;
}


JHandle *proxy_anewarray(ExecEnv *ee, ClassClass *fromClazz,
	HNET_shudo_metavm_VMAddress *addr, ClassClass *cb, int count) {
  JHandle *obj;

  char orig = GET_REMOTE_FLAG(ee);
  REMOTE_FLAG_OFF(ee);

  sysAssert(cb_Proxy != NULL);

  obj = (JHandle *)do_execute_java_method(ee, cb_Proxy,
	"get",
	"(Ljava/lang/Class;L" METAVM_PKG "VMAddress;Ljava/lang/Class;I)L" METAVM_PKG "Proxy;",
	NULL,	// struct methodblock
	TRUE,	// isStaticCall
	fromClazz, addr, cb, count);
#ifdef RUNTIME_DEBUG
  if (exceptionOccurred(ee)) {
    printf("exc. occurred.\n");
    fflush(stdout);
  }
#endif

  SET_REMOTE_FLAG(ee, orig);

#ifdef RUNTIME_DEBUG
  printf("proxy_anewarray() done. obj: 0x%08x\n", (int)obj);
  fflush(stdout);
#endif
  return obj;
}


JHandle *proxy_multianewarray(ExecEnv *ee, ClassClass *fromClazz,
	HNET_shudo_metavm_VMAddress *addr, ClassClass *cb,
	int dim, stack_item *stackpointer) {
  JNIEnv *env = EE2JNIEnv(ee);
  jintArray sizes;
  jint *csizes;
  JHandle *obj;
  int i;

  char orig = GET_REMOTE_FLAG(ee);
  REMOTE_FLAG_OFF(ee);
#ifdef RUNTIME_DEBUG
  printf("proxy_multianewarray(dim: %d) called.\n", dim);
  fflush(stdout);
#endif

  sizes = (*env)->NewIntArray(env, dim);
  csizes = (*env)->GetIntArrayElements(env, sizes, NULL);

  stackpointer += (dim - 1);
  for (i = 0; i < dim; i++) {
    int size = stackpointer->i;
#ifdef RUNTIME_DEBUG
    printf("  %d: %d\n", i, size);  fflush(stdout);
#endif
#ifndef NO_NULL_AND_ARRAY_CHECK
    if (size < 0) {	// NegativeArraySizeException
      SET_REMOTE_FLAG(ee, orig);
      return (JHandle *)-1;
    }
#endif
    csizes[i] = size;
    stackpointer--;
  }

  (*env)->ReleaseIntArrayElements(env, sizes, csizes, JNI_COMMIT);


  sysAssert(cb_Proxy != NULL);

  obj = (JHandle *)do_execute_java_method(ee, cb_Proxy,
	"get",
	"(Ljava/lang/Class;L" METAVM_PKG "VMAddress;Ljava/lang/Class;[I)L" METAVM_PKG "Proxy;",
	NULL,	// struct methodblock
	TRUE,	// isStaticCall
	fromClazz, addr, cb, DeRef(env, sizes));
#ifdef RUNTIME_DEBUG
  if (exceptionOccurred(ee)) {
    printf("exc. occurred.\n");
    fflush(stdout);
  }
#endif

  SET_REMOTE_FLAG(ee, orig);

#ifdef RUNTIME_DEBUG
  printf("proxy_multianewarray() done. obj: 0x%08x\n", (int)obj);
  fflush(stdout);
#endif
  return obj;
}


// definitions of proxy_get*field()
#define GET_FIELD(METHOD_NAME, SIG, CTYPE, POSTPROC) \
CTYPE proxy_##METHOD_NAME(ExecEnv *ee, JHandle *proxy, ... /*int32_t slot*/) {\
  CTYPE val;\
  int32_t ret;\
  va_list args;\
  \
  \
  char orig = GET_REMOTE_FLAG(ee);\
  REMOTE_FLAG_OFF(ee);\
  \
  va_start(args, proxy);\
  ret = (int32_t)do_execute_java_method_vararg(ee, proxy,\
	#METHOD_NAME, "(I)" #SIG,\
	mb_##METHOD_NAME,	/* struct methodblock */\
	FALSE,			/* isStaticCall */\
	args, (long *)&val, FALSE);\
  va_end(args);\
  \
  SET_REMOTE_FLAG(ee, orig);\
  \
  POSTPROC;\
  \
  return val;\
}

GET_FIELD(get32field, I, int32_t, val = (int32_t)ret);
GET_FIELD(get64field, J, int64_t, *((int32_t *)&val + 1) = ret);
GET_FIELD(getobjfield, Ljava/lang/Object;, JHandle *, val = (JHandle *)ret);

// definitions of proxy_aload*()
GET_FIELD(aload32, I, int32_t, val = (int32_t)ret);
GET_FIELD(aload64, J, int64_t, *((int32_t *)&val + 1) = ret);
GET_FIELD(aloadobj, Ljava/lang/Object;, JHandle *, val = (JHandle *)ret);


// definitions of proxy_put*field()
#define PUT_FIELD(METHOD_NAME, SIG, CTYPE) \
void proxy_##METHOD_NAME(ExecEnv *ee,\
	JHandle *proxy, int32_t slot, CTYPE val) {\
  char orig = GET_REMOTE_FLAG(ee);\
  REMOTE_FLAG_OFF(ee);\
  \
  do_execute_java_method(ee, proxy,\
	#METHOD_NAME, "(I" #SIG ")V",\
	mb_##METHOD_NAME,	/* struct methodblock */\
	FALSE,			/* isStaticCall */\
	slot, val);\
  \
  SET_REMOTE_FLAG(ee, orig);\
}

PUT_FIELD(put32field, I, int32_t);
PUT_FIELD(put64field, J, int64_t);
PUT_FIELD(putobjfield, Ljava/lang/Object;, JHandle *);

// definitions of proxy_astore*()
PUT_FIELD(astore32, I, int32_t);
PUT_FIELD(astore64, J, int64_t);
PUT_FIELD(astoreobj, Ljava/lang/Object;, JHandle *);


int32_t proxy_arraylength(ExecEnv *ee, JHandle *proxy) {
  int32_t len;

  char orig = GET_REMOTE_FLAG(ee);
  REMOTE_FLAG_OFF(ee);

  len = (int32_t)do_execute_java_method(ee, proxy,
	"arraylength", "()I",
	NULL,
	FALSE);	// isStaticCall
#ifdef RUNTIME_DEBUG
  if (exceptionOccurred(ee)) {
    printf("exc. occurred.\n");
    fflush(stdout);
  }
#endif

  SET_REMOTE_FLAG(ee, orig);
  return len;
}


int proxy_invoke(ExecEnv *ee, JHandle *proxy,
	struct methodblock *mb, stack_item *stackpointer) {
  int slot, mbindex;

  int args_size;	// in 4byte, mb->args_size
  stack_item *var_base, *spptr;
  int args_len;	// number of elements, NOT size
  char *sig = mb->fb.signature;
  char *p;

  HArrayOfObject *args = NULL;
  JHandle **args_body;  JHandle *args_elem;
  int ret_size;
  int i;

  char orig = GET_REMOTE_FLAG(ee);
  REMOTE_FLAG_OFF(ee);

#ifdef RUNTIME_DEBUG
  printf("proxy_invoke(sp: 0x%08x) called.\n", (int)stackpointer);
  printf("  proxy: 0x%08x\n", (int)proxy);
  fflush(stdout);
#endif

  slot = mb->fb.u.offset;
  if (slot == 0) {	// determine method index
    mbindex = mb - cbMethods(mb->fb.clazz);
  }
  else
    mbindex = 0;
#ifdef RUNTIME_DEBUG
    printf("  slot, mbindex: %d, %d\n", slot, mbindex);  fflush(stdout);
#endif


  // args_size and var_base
  args_size = mb->args_size;
  var_base = stackpointer + args_size - 1;
#ifdef RUNTIME_DEBUG
  printf("  args_size: %d\n", args_size);
  fflush(stdout);

  spptr = var_base;
  printf("  args on stack: 0x%x", *(int *)(spptr--));
  for (i = 1; i < args_size; i++)
    printf(", 0x%x", *(int *)(spptr--));
  printf("\n");
  fflush(stdout);
#endif


  // number of arguments
  args_len = 0;
  for (p = sig + 1; *p != ')'; p++) {
    args_len++;

    switch (*p) {
    case 'L':
      while (*p != ';')  p++;
      break;
    case '[':
      do { p++; } while (*p == '[');
      if (*p == 'L')  while (*p != ';')  p++;
    }
  }
#ifdef RUNTIME_DEBUG
  printf("  args_len: %d\n", args_len);
#endif

  p = sig + 1;

  if (args_len > 0) {
    int64_t longVal;
    double doubleVal;

    args = (HArrayOfObject *)ArrayAlloc(T_CLASS, args_len);
#if 0
    {	// (*env)->NewGlobalRef()
      extern JavaFrame *globalRefFrame;
      GLOBALREF_LOCK(EE2SysThread(ee));
      args_ref = (jobject)jni_addRef(globalRefFrame, args);
      GLOBALREF_UNLOCK(EE2SysThread(ee));
    }
#endif
    unhand(args)->body[args_len] = (HObject *)classJavaLangObject;
#ifdef RUNTIME_DEBUG
    printf("classJavaLangObject: 0x%08x\n", (int)classJavaLangObject);
    fflush(stdout);
#endif

    args_body = unhand(args)->body;
#ifdef RUNTIME_DEBUG
    printf("args, args_body: 0x%08x, 0x%08x\n", (int)args, (int)args_body);
    fflush(stdout);
#endif

#define COPY_ARG(JCLASS, SIG, QUOTED_SIG, SP_PREPROC, SP_ELEM, SP_POSTPROC) \
    case QUOTED_SIG:\
      args_elem = newobject(cb_##JCLASS, NULL, ee);\
      \
      SP_PREPROC;\
      do_execute_java_method(ee, args_elem,\
		"<init>", "(" #SIG ")V",\
		NULL,	/* struct methodblock */\
		FALSE,	/* isStaticCall */\
		SP_ELEM);\
      SP_POSTPROC;\
      args_body[i] = args_elem;\
      break

    spptr = var_base - 1;
    for (i = 0; i < args_len; i++) {
      switch (*p) {
      case 'L':
	args_body[i] = (spptr--)->h;
	while (*p != ';')  p++;
	break;
      case '[':
	p++;
	args_body[i] = (spptr--)->h;
	if (*p == 'L')  while (*p != ';')  p++;
	break;
      COPY_ARG(Boolean, Z, 'Z',		, spptr->i, spptr--);
      COPY_ARG(Byte, B, 'B',		, spptr->i, spptr--);
      COPY_ARG(Character, C, 'C',	, spptr->i, spptr--);
      COPY_ARG(Short, S, 'S',  		, spptr->i, spptr--);
      COPY_ARG(Integer, I, 'I',		, spptr->i, spptr--);
      COPY_ARG(Float, F, 'F',		, spptr->f, spptr--);
      COPY_ARG(Long, J, 'J',		memcpy(&longVal, spptr-1, 8);,
		longVal, spptr-=2);
      COPY_ARG(Double, D, 'D',		memcpy(&doubleVal, spptr-1, 8);,
		doubleVal, spptr-=2);
      }

      p++;
    }
  }	// if (args_len > 0)
  while (*p != ')')  p++;
  p++;
	// Now, p points sig. of return value
#ifdef RUNTIME_DEBUG
  {
    int i;
    for (i = 0; i < args_len; i++)
      printf("args[%d]: 0x%08x\n", i, (int)args_body[i]);
    fflush(stdout);
  }
#endif


  // calculate size of return value
  switch (*p) {
  case 'V':
    ret_size = 0;
    break;
  case 'L':  case '[':
  case 'Z':  case 'B':  case 'C':  case 'S':  case 'I':  case 'F':
    ret_size = 1;
    break;
  case 'J':  case 'D':
    ret_size = 2;
    break;
  default:
    printf("FATAL: invalid signature of return type : %c\n", *p);
    fflush(stdout);
    ret_size = 0;
    break;
  }

  // invoke
  {
    JavaFrame *cur_frame = ee->current_frame;
    stack_item *optop = cur_frame->optop;
    JHandle *ret;

#ifdef RUNTIME_DEBUG
    printf("args: 0x%08x\n", (int)args);
    fflush(stdout);
#endif

    ret = (JHandle *)do_execute_java_method(ee, proxy,
		"invoke",
		"(II[Ljava/lang/Object;)Ljava/lang/Object;",
		NULL,	// struct methodblock *
		FALSE,	// isStaticCall
		slot, mbindex, args);

#if 0
    (*env)->DeleteGlobalRef(env, args_ref);
#endif

    if (exceptionOccurred(ee)) {
#ifdef RUNTIME_DEBUG
      printf("proxy_invoke(): an exception thrown.\n");
      fflush(stdout);
#endif
      SET_REMOTE_FLAG(ee, orig);
      return -1;
    }
#ifdef RUNTIME_DEBUG
    printf("returned obj: 0x%08x\n", (int)ret);
    fflush(stdout);
#endif

    switch (*p) {
    case 'L':  case '[':
      optop->h = ret;
      cur_frame->optop++;
      break;
    case 'Z':
      optop->i = unhand((Hjava_lang_Boolean *)ret)->value;
      cur_frame->optop++;  break;
    case 'B':
      optop->i = unhand((Hjava_lang_Byte *)ret)->value;
      cur_frame->optop++;  break;
    case 'C':
      optop->i = unhand((Hjava_lang_Character *)ret)->value;
      cur_frame->optop++;  break;
    case 'S':
      optop->i = unhand((Hjava_lang_Short *)ret)->value;
      cur_frame->optop++;  break;
    case 'I':
      optop->i = unhand((Hjava_lang_Integer *)ret)->value;
#ifdef RUNTIME_DEBUG
      printf("ret: (int)%d\n", optop->i);  fflush(stdout);
#endif
      cur_frame->optop++;  break;
    case 'J':
      {
	int64_t val = unhand((Hjava_lang_Long *)ret)->value;
	optop[0].i = ((int32_t *)&val)[0];
	optop[1].i = ((int32_t *)&val)[1];
#ifdef RUNTIME_DEBUG
	printf("ret: (long)%lld\n", *((int64_t *)optop));  fflush(stdout);
#endif
	cur_frame->optop += 2;
      }
      break;
    case 'F':
      optop->f = unhand((Hjava_lang_Float *)ret)->value;
#ifdef RUNTIME_DEBUG
      printf("ret: (float)%g\n", *((float *)optop));  fflush(stdout);
#endif
      cur_frame->optop++;  break;
    case 'D':
      {
	double val = unhand((Hjava_lang_Double *)ret)->value;
	optop[0].i = ((int32_t *)&val)[0];
	optop[1].i = ((int32_t *)&val)[1];
#ifdef RUNTIME_DEBUG
	printf("ret: (double)%g\n", *((double *)optop));  fflush(stdout);
#endif
	cur_frame->optop += 2;
      }
      break;
    }
  }

  SET_REMOTE_FLAG(ee, orig);

#ifdef RUNTIME_DEBUG
  printf("proxy_invoke() done.\n");
  fflush(stdout);
#endif

  // set return value to %edx and %ecx
  {
    register stack_item *optop asm("%edi");
    optop = ee->current_frame->optop - ret_size;

    asm("movl  %0,%%edx" : : "m" (optop[0].i) : "edx");
    asm("movl  %0,%%ecx" : : "m" (optop[1].i) : "ecx");
  }

  return ret_size;
}


int proxy_monitorenter(ExecEnv *ee, JHandle *proxy) {
  do_execute_java_method(ee, proxy,
		"monitorEnter", "()V",
		NULL,	// struct methodblock
		FALSE);	// isStaticCall

  if (exceptionOccurred(ee))
    return -1;
  else
    return 0;
}


int proxy_monitorexit(ExecEnv *ee, JHandle *proxy) {
  do_execute_java_method(ee, proxy,
		"monitorExit", "()V",
		NULL,	// struct methodblock
		FALSE);	// isStaticCall

  if (exceptionOccurred(ee))
    return -1;
  else
    return 0;
}


JHandle *proxy_getObjCopy(ExecEnv *ee, JHandle *proxy) {
  JHandle *ret;

  char orig = GET_REMOTE_FLAG(ee);
  REMOTE_FLAG_OFF(ee);

  ret = (JHandle *)do_execute_java_method(ee, proxy,
	"getObjCopy", "()Ljava/lang/Object;",
	mb_getObjCopy,
	FALSE);	// isStaticCall
#ifdef RUNTIME_DEBUG
  if (exceptionOccurred(ee)) {
    printf("exc. occurred.\n");
    fflush(stdout);
  }
#endif

  SET_REMOTE_FLAG(ee, orig);
  return ret;
}


JHandle *proxy_getArrayCopy(ExecEnv *ee, JHandle *proxy,
				int32_t position, int32_t length) {
  JHandle *ret;

  char orig = GET_REMOTE_FLAG(ee);
  REMOTE_FLAG_OFF(ee);

  ret = (JHandle *)do_execute_java_method(ee, proxy,
	"getArrayCopy", "(II)Ljava/lang/Object;",
	mb_getArrayCopy,
	FALSE,	// isStaticCall
	position, length);
#ifdef RUNTIME_DEBUG
  if (exceptionOccurred(ee)) {
    printf("exc. occurred.\n");
    fflush(stdout);
  }
#endif

  SET_REMOTE_FLAG(ee, orig);
  return ret;
}


void proxy_aryBlockLoad(ExecEnv *ee, JHandle *proxy,
		JHandle *dst, int32_t srcpos, int32_t len, int32_t dstpos) {
  char orig = GET_REMOTE_FLAG(ee);
  REMOTE_FLAG_OFF(ee);

  (int32_t)do_execute_java_method(ee, proxy,
	"aryBlockLoad", "(Ljava/lang/Object;III)V",
	mb_aryBlockLoad,
	FALSE,	// isStaticCall
	dst, srcpos, len, dstpos);
#ifdef RUNTIME_DEBUG
  if (exceptionOccurred(ee)) {
    printf("exc. occurred.\n");
    fflush(stdout);
  }
#endif

  SET_REMOTE_FLAG(ee, orig);
}


void proxy_aryBlockStore(ExecEnv *ee, JHandle *proxy,
		JHandle *src, int32_t srcpos, int32_t len, int32_t dstpos) {
  char orig = GET_REMOTE_FLAG(ee);
  REMOTE_FLAG_OFF(ee);

  (int32_t)do_execute_java_method(ee, proxy,
	"aryBlockStore", "(Ljava/lang/Object;III)V",
	mb_aryBlockStore,
	FALSE,	// isStaticCall
	src, srcpos, len, dstpos);
#ifdef RUNTIME_DEBUG
  if (exceptionOccurred(ee)) {
    printf("exc. occurred.\n");
    fflush(stdout);
  }
#endif

  SET_REMOTE_FLAG(ee, orig);
}
