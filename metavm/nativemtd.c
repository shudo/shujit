/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1998,1999,2000,2001 Kazuyuki Shudo

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

#include "native.h"	// for type ExecEnv
#include "NET_shudo_metavm_Proxy_old.h"


// MetaVM_*()s replaces native methods that are provided by JVM.

JNIEXPORT void JNICALL
MetaVM_ArrayCopy(JNIEnv *, jclass, jobject, jint, jobject, jint, jint);

#define OBJ "Ljava/lang/Object;"
static JNINativeMethod methods_javaLangSystem[] = {
  {"arraycopy",	"(" OBJ "I" OBJ "II)V", (void *)&MetaVM_ArrayCopy},
};


/*
 * Replace native methods.
 */
JNIEXPORT void JNICALL Java_NET_shudo_metavm_MetaVM_initializationForMetaVM
  (JNIEnv *env, jclass ignored) {
  jclass clazz;

  clazz = (*env)->FindClass(env, "java/lang/System");
  (*env)->RegisterNatives(env, clazz, methods_javaLangSystem, 1);
}


JNIEXPORT void JNICALL
MetaVM_ArrayCopy(JNIEnv *env, jclass clazz,
	jobject src, jint src_pos, jobject dst, jint dst_pos, jint len) {
  ExecEnv *ee = JNIEnv2EE(env);
  JHandle *srch, *dsth;
  jboolean srcp = JNI_FALSE, dstp = JNI_FALSE;
  int32_t src_len, dst_len;
#if 0
printf("MetaVM_ArrayCopy() called.\n");
fflush(stdout);
#endif

  srch = DeRef(env, src);
  dsth = DeRef(env, dst);

  // null check
  if ((srch == 0) || (dsth == 0)) {
    ThrowNullPointerException(JNIEnv2EE(env), 0);
    return;
  }

  // check whether proxy or not and get the length
  if (obj_flags(srch) == T_NORMAL_OBJECT) {
    if (obj_classblock(srch) == cb_Proxy) {	// src is Proxy
      srcp = JNI_TRUE;
      src_len = unhand((HNET_shudo_metavm_Proxy *)srch)->length;
    }
    else {
      ThrowArrayStoreException(0, 0);  return;
    }
  }
  else
    src_len = obj_length(srch);

  if (obj_flags(dsth) == T_NORMAL_OBJECT) {
    if (obj_classblock(dsth) == cb_Proxy) {	// dst is Proxy
      dstp = JNI_TRUE;
      dst_len = unhand((HNET_shudo_metavm_Proxy *)dsth)->length;
    }
    else {
      ThrowArrayStoreException(0, 0);  return;
    }
  }
  else
    dst_len = obj_length(dsth);

  // array range check
  if ((len < 0) || (src_pos < 0) || (dst_pos < 0) ||
      (len + src_pos > src_len) ||
      (len + dst_pos > dst_len)) {
    ThrowArrayIndexOutOfBoundsException(0, 0);
  }


  if (srcp && !dstp) {
    proxy_aryBlockLoad(ee, srch, dsth, src_pos, len, dst_pos);
  }
  else if (!srcp && dstp) {
    proxy_aryBlockStore(ee, dsth, srch, src_pos, len, dst_pos);
  }
  else if (srcp && dstp) {
    srch = proxy_getArrayCopy(ee, srch, src_pos, len);
    // get a partial copy of the remote array
    src = MkRefLocal(env, srch);
    src_pos = 0;

    proxy_aryBlockStore(ee, dsth, srch, src_pos, len, dst_pos);
  }
  else {
    JVM_ArrayCopy(env, clazz, src, src_pos, dst, dst_pos, len);
  }
}
