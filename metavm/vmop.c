/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1998,1999,2000 Kazuyuki Shudo

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

#include "metavm.h"
#include "../compiler.h"	// for CB_INITIALIZED

#include "native.h"	// for old style native methods
#include "sys_api.h"	// sys*()

#include "NET_shudo_metavm_VMOperations.h"


#define T_OBJECT	0
#define T_BOOLEAN	4
#define T_CHAR		5


static jclass jclz_NoSuchFieldException = NULL;
static jclass jclz_NoSuchMethodException = NULL;

#define FIELD_ACC_NULL_CHECK(VAR) \
  if (! VAR) {\
    if (!jclz_NoSuchFieldException) {\
      jclz_NoSuchFieldException =\
	(*env)->FindClass(env, "java/lang/NoSuchFieldException");\
    }\
    (*env)->ThrowNew(env, jclz_NoSuchFieldException, NULL);\
  }


//
// Local Functions
//
static jobject invokeMethod(JNIEnv *env,
	jobject, jmethodID, jobjectArray);



JNIEXPORT jobject JNICALL Java_NET_shudo_metavm_VMOperations_instantiate
  (JNIEnv *env, jclass clazz, jclass tgtclz) {
  jobject obj;
#ifdef RUNTIME_DEBUG
  printf("vmop#instantiate called.\n");
  fflush(stdout);
  {
    ClassClass *clz = (ClassClass *)DeRef(env, tgtclz);
    printf("  class: %s\n", (clz?cbName(clz):"null"));
  }
  fflush(stdout);
#endif

  // initialize the class
  {
    ClassClass *cb = (ClassClass *)DeRef(env, tgtclz);
    if (!CB_INITIALIZED(cb))  InitClass(cb);
  }

  obj = (*env)->AllocObject(env, tgtclz);
#ifdef RUNTIME_DEBUG
  if ((*env)->ExceptionOccurred(env)) {
    printf("  exc. occurred.\n");
    (*env)->ExceptionDescribe(env);
  }
  else {
    printf("  exc. didn't occur.\n");
  }
  fflush(stdout);
#endif
  return obj;
}


JNIEXPORT jobject JNICALL Java_NET_shudo_metavm_VMOperations_newarray
  (JNIEnv *env, jclass clazz, jint type, jint count) {
  jobject array = NULL;

  switch (type) {
  case T_BOOLEAN:	array = (*env)->NewBooleanArray(env, count);  break;
  case T_CHAR:		array = (*env)->NewCharArray(env, count);  break;
  case T_FLOAT:		array = (*env)->NewFloatArray(env, count);  break;
  case T_DOUBLE:	array = (*env)->NewDoubleArray(env, count);  break;
  case T_BYTE:		array = (*env)->NewByteArray(env, count);  break;
  case T_SHORT:		array = (*env)->NewShortArray(env, count);  break;
  case T_INT:		array = (*env)->NewIntArray(env, count);  break;
  case T_LONG:		array = (*env)->NewLongArray(env, count);  break;
  }

  return array;
}


JNIEXPORT jobject JNICALL Java_NET_shudo_metavm_VMOperations_anewarray
  (JNIEnv *env, jclass clazz, jclass tgtclz, jint count) {

  // initialize the class
  {
    ClassClass *cb = (ClassClass *)DeRef(env, tgtclz);
    if (!CB_INITIALIZED(cb))  InitClass(cb);
  }

  return (*env)->NewObjectArray(env, count, tgtclz, NULL);
}


JNIEXPORT jobject JNICALL Java_NET_shudo_metavm_VMOperations_multianewarray
  (JNIEnv *env , jclass clazz, jclass aryclz, jintArray sizes) {
  int dim;
  jint *csizes;
  stack_item *s;
  int i;
  JHandle *array;

  dim = (int)(*env)->GetArrayLength(env, sizes);

  csizes = (*env)->GetIntArrayElements(env, sizes, NULL);

  s = malloc(sizeof(stack_item) * dim);
  for (i = 0; i < dim; i++) {
    s[i].i = (int)csizes[i];
  }

  (*env)->ReleaseIntArrayElements(env, sizes, csizes, JNI_ABORT);
				// JNI_ABORT: copy back isn't required

  // initialize the class
  {
    ClassClass *cb = (ClassClass *)DeRef(env, aryclz);
    if (!CB_INITIALIZED(cb))  InitClass(cb);
  }

  array = MultiArrayAlloc((int)dim, (ClassClass *)DeRef(env, aryclz), s);

  free(s);

  return MK_REF_LOCAL(env, array);
}



JNIEXPORT jint JNICALL Java_NET_shudo_metavm_VMOperations_get32Field
  (JNIEnv *env, jclass clazz, jobject obj, jint slot) {
  return (jint)obj_getslot((JHandle *)DeRef(env, obj), slot);
}

JNIEXPORT jlong JNICALL Java_NET_shudo_metavm_VMOperations_get64Field
  (JNIEnv *env, jclass clazz, jobject obj, jint slot) {
  JHandle *h = (JHandle *)DeRef(env, obj);
  jlong val;
  int32_t *valptr = (int32_t *)&val;

  valptr[0] = obj_getslot(h, slot);
  valptr[1] = obj_getslot(h, slot + 1);

  return val;
}

JNIEXPORT jobject JNICALL Java_NET_shudo_metavm_VMOperations_getObjectField
  (JNIEnv *env, jclass clazz, jobject obj, jint slot) {
  return MK_REF_LOCAL(env, obj_getslot((JHandle *)DeRef(env, obj), slot));
}

JNIEXPORT void JNICALL Java_NET_shudo_metavm_VMOperations_put32Field
  (JNIEnv *env, jclass clazz, jobject obj, jint slot, jint val) {
  obj_setslot((JHandle *)DeRef(env, obj), slot, val);
}

JNIEXPORT void JNICALL Java_NET_shudo_metavm_VMOperations_put64Field
  (JNIEnv *env, jclass clazz, jobject obj, jint slot, jlong val) {
  JHandle *h = (JHandle *)DeRef(env, obj);
  int32_t *valptr = (int32_t *)&val;

  obj_setslot(h, slot, valptr[0]);
  obj_setslot(h, slot + 1, valptr[1]);
}

JNIEXPORT void JNICALL Java_NET_shudo_metavm_VMOperations_putObjectField
  (JNIEnv *env, jclass clazz, jobject obj, jint slot, jobject val) {
  obj_setslot((JHandle *)DeRef(env, obj), slot, (int32_t)DeRef(env, val));
}


static jclass jclz_Boolean = NULL;
static jclass jclz_Byte = NULL;
static jclass jclz_Character = NULL;
static jclass jclz_Short = NULL;
static jclass jclz_Integer = NULL;
static jclass jclz_Long = NULL;
static jclass jclz_Float = NULL;
static jclass jclz_Double = NULL;

static jfieldID fid_BooleanValue = NULL;
static jfieldID fid_ByteValue = NULL;
static jfieldID fid_CharacterValue = NULL;
static jfieldID fid_ShortValue = NULL;
static jfieldID fid_IntegerValue = NULL;
static jfieldID fid_LongValue = NULL;
static jfieldID fid_FloatValue = NULL;
static jfieldID fid_DoubleValue = NULL;

static jmethodID mid_BooleanInit = NULL;
static jmethodID mid_ByteInit = NULL;
static jmethodID mid_CharacterInit = NULL;
static jmethodID mid_ShortInit = NULL;
static jmethodID mid_IntegerInit = NULL;
static jmethodID mid_LongInit = NULL;
static jmethodID mid_FloatInit = NULL;
static jmethodID mid_DoubleInit = NULL;


JNIEXPORT jobject JNICALL Java_NET_shudo_metavm_VMOperations_invoke__Ljava_lang_Object_2Ljava_lang_String_2Ljava_lang_String_2_3Ljava_lang_Object_2
  (JNIEnv *env, jclass clazz,
	jobject obj, jstring name, jstring sig, jobjectArray args) {
  const char *cname, *csig;
  jclass objClazz;  jmethodID mid;
  jobject result;

#ifdef RUNTIME_DEBUG
  printf("vmop#invoke(name) called.\n");
  printf("  obj: 0x%08x\n", (int)obj);
  fflush(stdout);
#endif
  cname = (*env)->GetStringUTFChars(env, name, NULL);
  csig = (*env)->GetStringUTFChars(env, sig, NULL);

  objClazz = (*env)->GetObjectClass(env, obj);
#ifdef RUNTIME_DEBUG
printf("  objClazz: 0x%08x\n", (int)objClazz);
fflush(stdout);
#endif

  // get method ID
  mid = (*env)->GetMethodID(env, objClazz, cname, csig);
  if (!mid) {
    if (!jclz_NoSuchMethodException) {
      jclz_NoSuchMethodException =
	(*env)->FindClass(env, "java/lang/NoSuchMethodException");
      jclz_NoSuchMethodException =
	(*env)->NewGlobalRef(env, jclz_NoSuchMethodException);
		// register as global ref.
    }
    (*env)->ThrowNew(env, jclz_NoSuchMethodException, NULL);
  }

  // invoke the method
  result = invokeMethod(env, obj, mid, args);

  (*env)->ReleaseStringUTFChars(env, name, cname);
  (*env)->ReleaseStringUTFChars(env, sig, csig);

#ifdef RUNTIME_DEBUG
  printf("vmop#invoke done.\n");
  fflush(stdout);
#endif

  return result;
}


JNIEXPORT jobject JNICALL Java_NET_shudo_metavm_VMOperations_invoke__Ljava_lang_Object_2II_3Ljava_lang_Object_2
  (JNIEnv *env, jclass clazz,
	jobject receiver, jint slot, jint mbindex, jobjectArray args) {
  jclass objClazz;
  jmethodID mid;
  jobject result;

#ifdef RUNTIME_DEBUG
  printf("vmop#invoke(slot, mbindex) called.\n");
  printf("  receiver: 0x%x\n", (int)receiver);
  printf("  slot, mbindex: %d, %d\n", (int)slot, (int)mbindex);
  fflush(stdout);
#endif

  if (receiver == NULL) {
#ifdef RUNTIME_DEBUG
    printf("  receiver is null !\n");
    fflush(stdout);
#endif
    ThrowNullPointerException(JNIEnv2EE(env), 0);	// not in JNI style
    return NULL;
  }

  objClazz = (*env)->GetObjectClass(env, receiver);
#ifdef RUNTIME_DEBUG
  printf("  objClazz: 0x%x\n", (int)objClazz);
  fflush(stdout);
  printf("  class name: %s\n", cbName((ClassClass *)DeRef(env, objClazz)));
  fflush(stdout);
#endif

  // get method ID
  if (slot != 0) {
    mid = (jmethodID)
	mt_slot(obj_array_methodtable((JHandle *)DeRef(env, receiver)), slot);
    if (!mid)  return NULL;	// haven't to be called
  }
  else {
    mid = (jmethodID)
	(cbMethods((ClassClass *)DeRef(env, objClazz)) + mbindex);
  }
#ifdef RUNTIME_DEBUG
  printf("  mb: 0x%x\n", (int)mid);
  if (slot != 0)
    printf("  name: %s\n", (int)((struct methodblock *)mid)->fb.name);
  fflush(stdout);
#endif

  // invoke the method
  result = invokeMethod(env, receiver, mid, args);

#ifdef RUNTIME_DEBUG
  printf("vmop#invoke done.\n");
  fflush(stdout);
#endif

  return result;
}


static jobject invokeMethod(JNIEnv *env,
	jobject receiver, jmethodID mid, jobjectArray args) {
  jsize arrayLength;
#define PREALLOC_SIZE 10
  jvalue cargsArray[PREALLOC_SIZE];
  jvalue *cargs;
  jobject result;
  char *csig = ((struct methodblock *)mid)->fb.signature;
  int i;

  // prepare arguments
  if (args) {
    arrayLength = (*env)->GetArrayLength(env, args);
#ifdef RUNTIME_DEBUG
    printf("  arrayLength: %d\n", (int)arrayLength);
    fflush(stdout);
#endif
    if (arrayLength > PREALLOC_SIZE)
      cargs = (jvalue *)sysMalloc(sizeof(jvalue) * arrayLength);
    else
      cargs = cargsArray;

    {
      jobject elem;
      const char *p;

#define COPY_ARG(JCLASS, JTYPE, SIG, sig) \
  if (!fid_##JCLASS##Value) {\
    if (!jclz_##JCLASS) {\
      jclz_##JCLASS = (*env)->FindClass(env, "java/lang/" #JCLASS);\
      jclz_##JCLASS = (*env)->NewGlobalRef(env, jclz_##JCLASS);\
		/* register as global ref. */\
    }\
    fid_##JCLASS##Value=\
	(*env)->GetFieldID(env, jclz_##JCLASS, "value", #SIG);\
  }\
  (cargs + i)->##sig =\
	(*env)->Get##JTYPE##Field(env, elem, fid_##JCLASS##Value);

      for (i = 0, p = csig + 1; i < arrayLength; i++) {
	if (*p == ')')  break;
	elem = (*env)->GetObjectArrayElement(env, args, i);
#ifdef RUNTIME_DEBUG
	printf("  arg[%d] sig: %c, elem: 0x%x\n", i, *p, (int)elem);
	fflush(stdout);
#endif

	switch (*p) {
	case 'L':
	  (cargs + i)->l = elem;
	  while (*p != ';')  p++;
	  break;
	case '[':
	  (cargs + i)->l = elem;
	  do { p++; } while (*p == '[');
	  if (*p == 'L')
	    while (*p != ';')  p++;
	  break;
	case 'Z':  COPY_ARG(Boolean, Boolean, Z, z);  break;
	case 'B':  COPY_ARG(Byte, Byte, B, b);  break;
	case 'C':  COPY_ARG(Character, Char, C, c);  break;
	case 'S':  COPY_ARG(Short, Short, S, s);  break;
	case 'I':  COPY_ARG(Integer, Int, I, i);  break;
	case 'J':  COPY_ARG(Long, Long, J, j);  break;
	case 'F':  COPY_ARG(Float, Float, F, f);  break;
	case 'D':  COPY_ARG(Double, Double, D, d);  break;
	}
	p++;
      }
    }
  }	// if (args)
  else {
    cargs = NULL;
  }

  // invoke
  {
    const char *p = csig;
    while (*p != ')')  p++;
    p++;

#define INVOKE_METHOD(JCLASS, JTYPE, jtype, SIG)\
     {\
	j##jtype ret;\
	ret = (*env)->Call##JTYPE##MethodA(env, receiver, mid, cargs);\
	if (!jclz_##JCLASS) {\
	  jclz_##JCLASS = (*env)->FindClass(env, "java/lang/" #JCLASS);\
	  jclz_##JCLASS = (*env)->NewGlobalRef(env, jclz_##JCLASS);\
	}\
	if (!mid_##JCLASS##Init) {\
	  mid_##JCLASS##Init = (*env)->GetMethodID(env, jclz_##JCLASS, "<init>", "(" #SIG ")V");\
	}\
	result = (*env)->NewObject(env, jclz_##JCLASS, mid_##JCLASS##Init, ret);\
      }

    switch (*p) {
    case 'V':
      (*env)->CallVoidMethodA(env, receiver, mid, cargs);
      result = NULL;
      break;
    case 'L':
    case '[':
      result = (*env)->CallObjectMethodA(env, receiver, mid, cargs);
      break;
    case 'Z':  INVOKE_METHOD(Boolean, Boolean, boolean, Z);  break;
    case 'B':  INVOKE_METHOD(Byte, Byte, byte, B);  break;
    case 'C':  INVOKE_METHOD(Character, Char, char, C);  break;
    case 'S':  INVOKE_METHOD(Short, Short, short, S);  break;
    case 'I':  INVOKE_METHOD(Integer, Int, int, I);  break;
    case 'J':  INVOKE_METHOD(Long, Long, long, J);  break;
    case 'F':  INVOKE_METHOD(Float, Float, float, F);  break;
    case 'D':  INVOKE_METHOD(Double, Double, double, D);  break;
    }
  }

  if (arrayLength > PREALLOC_SIZE)
    sysFree(cargs);

  return result;
}


JNIEXPORT void JNICALL Java_NET_shudo_metavm_VMOperations_monitorEnter
  (JNIEnv *env, jclass clazz, jobject obj) {
  (*env)->MonitorEnter(env, obj);
}


JNIEXPORT void JNICALL Java_NET_shudo_metavm_VMOperations_monitorExit
  (JNIEnv *env, jclass clazz, jobject obj) {
  (*env)->MonitorExit(env, obj);
}


JNIEXPORT void JNICALL Java_NET_shudo_metavm_VMOperations_printStackTrace
  (JNIEnv *env, jclass clazz) {
  showStackFrames(JNIEnv2EE(env));	// in runtime.c
}


JNIEXPORT jboolean JNICALL Java_NET_shudo_metavm_VMOperations_isNativeMethod
  (JNIEnv *env, jclass clz, jclass clazz, jint slot, jint mbindex) {
  ClassClass *cb = (ClassClass *)DeRef(env, clazz);
  struct methodblock *mb;

  if (slot != 0)
    mb = mt_slot(cbMethodTable(cb), slot);
  else if (mbindex != 0)
    mb = cbMethods(cb) + mbindex;
  else
    return JNI_FALSE;	// constructor

  return ((mb->fb.access & ACC_NATIVE) != 0);
}


JNIEXPORT jstring JNICALL Java_NET_shudo_metavm_VMOperations_MethodName
  (JNIEnv *env, jclass clz, jclass clazz, jint slot, jint mbindex) {
  ClassClass *cb = (ClassClass *)DeRef(env, clazz);
  struct methodblock *mb;
  char *name;

  if (slot != 0) {
    mb = mt_slot(cbMethodTable(cb), slot);
    name = mb->fb.name;
  }
  else if (mbindex != 0) {
    mb = cbMethods(cb) + mbindex;
    name = mb->fb.name;
  }
  else
    name = "<init>";	// constructor

  return MK_REF_LOCAL(env, (JHandle *)makeJavaStringUTF(name));
}


JNIEXPORT jstring JNICALL Java_NET_shudo_metavm_VMOperations_MethodSignature
  (JNIEnv *env, jclass clz, jclass clazz, jint slot, jint mbindex) {
  ClassClass *cb = (ClassClass *)DeRef(env, clazz);
  struct methodblock *mb;
  char *sig;

  if (slot != 0) {
    mb = mt_slot(cbMethodTable(cb), slot);
    sig = mb->fb.name;
  }
  else if (mbindex != 0) {
    mb = cbMethods(cb) + mbindex;
    sig = mb->fb.name;
  }
  else
    sig = "()V";	// constructor

  return MK_REF_LOCAL(env, (JHandle *)makeJavaStringUTF(sig));
}
