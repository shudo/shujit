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
#include "sys_api.h"	// for sys*()
#include "java_lang_Thread.h"	// for ->eetop

#include "NET_shudo_metavm_MetaVM.h"

#if JDK_VER >= 12
#  include "typedefs_md.h"	// for ll2ptr()
#endif


JNIEXPORT jboolean JNICALL Java_NET_shudo_metavm_MetaVM_remoteTransparency__
  (JNIEnv *env, jclass clazz) {
  ExecEnv *ee = JNIEnv2EE(env);

  if (!ee)  return JNI_FALSE;

  return GET_REMOTE_FLAG(ee);
}

JNIEXPORT jboolean JNICALL Java_NET_shudo_metavm_MetaVM_remoteTransparency__Z
  (JNIEnv *env, jclass clazz, jboolean flag) {
  ExecEnv *ee = JNIEnv2EE(env);
  jboolean orig;
#if 0
  printf("remoteTransparency(%s) called.\n", (flag?"true":"false"));
  fflush(stdout);
#endif
  if (!ee)  return JNI_FALSE;
  orig = (jboolean)GET_REMOTE_FLAG(ee);

  SET_REMOTE_FLAG(ee, flag);	// JNI_TRUE or JNI_FALSE

  return orig;
}

JNIEXPORT jboolean JNICALL Java_NET_shudo_metavm_MetaVM_remoteTransparency__Ljava_lang_Thread_2Z
  (JNIEnv *env, jclass clazz, jobject thr, jboolean flag) {
  ExecEnv *ee;
  jboolean orig;

  if (!thr) {
#ifdef RUNTIME_DEBUG
    printf("remoteTransparency(): thread is null.\n");
    fflush(stdout);
#endif
    return JNI_FALSE;
  }
#if 0
  printf("remoteTransparency(0x%x, %s) called.\n",
	(int)DeRef(env, thr), (flag?"true":"false"));
  fflush(stdout);
#endif

  ee = (ExecEnv *)
#if JDK_VER >=12
	ll2ptr(unhand((Hjava_lang_Thread *)DeRef(env, thr))->eetop);
#else
	*(ExecEnv **)&(unhand((Hjava_lang_Thread *)DeRef(env, thr))->eetop);
#endif
  if (!ee)  return JNI_FALSE;
  orig = (jboolean)GET_REMOTE_FLAG(ee);
  SET_REMOTE_FLAG(ee, flag);

  return orig;
}


JNIEXPORT jobject JNICALL Java_NET_shudo_metavm_MetaVM_instantiationVM0__LNET_shudo_metavm_VMAddress_2
  (JNIEnv *env, jclass clazz, jobject vmaddr) {
  ExecEnv *ee = JNIEnv2EE(env);
  JHandle *h;

#ifdef RUNTIME_DEBUG
  printf("remote flag: 0x%x (%x)\n", GET_REMOTE_FLAG(ee) & 0xff, ee);
  fflush(stdout);
#endif

  h = REMOTE_ADDR(ee);
  REMOTE_ADDR(ee) = (vmaddr != NULL) ? (JHandle *)DeRef(env, vmaddr) : NULL;

  return (h != NULL) ? MK_REF_LOCAL(env, (void *)h) : NULL;
}
