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

#include "metavm.h"	// for MK_REF_LOCAL()

#include "NET_shudo_metavm_ObjectID.h"
#include "native.h"	// for MkRefLocal()


JNIEXPORT jint JNICALL Java_NET_shudo_metavm_ObjectID_idByObject
  (JNIEnv *env, jclass clazz, jobject obj) {
#ifdef RUNTIME_DEBUG
  printf("idByObject() called.\n");
  fflush(stdout);
#endif
  return (jint)DeRef(env, obj);
}


JNIEXPORT jobject JNICALL Java_NET_shudo_metavm_ObjectID_objectById
  (JNIEnv *env, jclass clazz, jint id) {
#ifdef RUNTIME_DEBUG
  printf("objectById() called.\n");
  fflush(stdout);
#endif
  return (jobject)MK_REF_LOCAL(env, id);
}
