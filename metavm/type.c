/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1998,1999 Kazuyuki Shudo

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

#include "NET_shudo_metavm_TypeUtil.h"

#include "sys_api.h"	// for sys*()

#include "metavm.h"


static ClassClass **cbTable = NULL;
static int cbTableLen = 0;
#define DEFAULT_CB_TABLE_SIZE	8;
static int cbTableSize = DEFAULT_CB_TABLE_SIZE;

JNIEXPORT void JNICALL Java_NET_shudo_metavm_TypeUtil_addCheckPassType
  (JNIEnv *env, jclass clz, jclass entry) {
#ifdef RUNTIME_DEBUG
  printf("addCheckPassType called: %s\n",
	(entry ? cbName((ClassClass *)DeRef(env, entry)):"null"));
  fflush(stdout);
#endif
  if (!entry)  return;

  if (!cbTable) {
    cbTable = (ClassClass **)sysMalloc(sizeof(ClassClass *) * cbTableSize);
  }

  if (cbTableLen >= cbTableSize) {
    cbTableSize << 1;
    cbTable = (ClassClass **)sysRealloc((void *)cbTable,
				sizeof(ClassClass *) * cbTableSize);
  }

  cbTable[cbTableLen++] = (ClassClass *)DeRef(env, entry);
}

JNIEXPORT void JNICALL Java_NET_shudo_metavm_TypeUtil_clearCheckPassType
  (JNIEnv *env, jclass clz) {
  cbTableLen = 0;
}

int isCheckPassType(ClassClass *cb) {
  int i;
#if 0
  printf("isCheckPassType called: %s\n", (cb ? cbName(cb) : "null"));
  fflush(stdout);
#endif

  if (cbTableLen <= 0) return 0;
  if (!cb)  return 0;

  for (i = 0; i < cbTableLen; i++)
    if (cbTable[i] == cb)  return 1;
  return 0;
}


JNIEXPORT void JNICALL Java_NET_shudo_metavm_TypeUtil_forceToImplement0
  (JNIEnv *env, jclass clz, jclass jclazz, jclass jintf) {
  forceToImplement(JNIEnv2EE(env),
	(ClassClass *)DeRef(env, jclazz), (ClassClass *)DeRef(env, jintf));
}


#undef FORCE_IMPL_DEBUG
void forceToImplement(ExecEnv* ee, ClassClass *clazz, ClassClass *intf) {
  int i, j;

  const int ITEM_SIZE = sizeof(cbIntfMethodTable(clazz)->itable[0]);
  struct imethodtable *clazz_imtable, *imtable, *intf_imtable;
  int clazz_icount, icount, intf_icount;
  int clazz_mcount, mcount, intf_mcount;

  size_t itable_size, alloc_size;
  char *alloced;

  if (!clazz || !intf)  return;

#ifdef FORCE_IMPL_DEBUG
  printf("forceImplement called: %s, %s\n", cbName(clazz), cbName(intf));
  fflush(stdout);
#endif

  if (ImplementsInterface(clazz, intf, ee)) {
#ifdef FORCE_IMPL_DEBUG
    printf("  already implements.\n");  fflush(stdout);
#endif
    return;
  }

#if 0	// can't touch implements and implements_count
  cbImplementsCount(clazz)++;
#endif

#if JDK_VER >= 12
  if (!CCIs(clazz, InterfacePrepared)) {
#  ifdef FORCE_IMPL_DEBUG
    printf("  InterfacePrepared is false.\n");  fflush(stdout);
#  endif
    LinkClass(clazz);
  }
#endif	// JDK_VER

  clazz_imtable = cbIntfMethodTable(clazz);
  clazz_icount = clazz_imtable->icount;
  intf_imtable = cbIntfMethodTable(intf);
  intf_icount = intf_imtable->icount;

  clazz_mcount = 0;
  for (i = 0; i < clazz_icount; i++) {
    ClassClass *cb;
    int methods;
    cb = clazz_imtable->itable[i].classdescriptor;
    methods = cbMethodsCount(cb);
#ifdef FORCE_IMPL_DEBUG
    printf("  clazz: %s (methods: %d)\n", cbName(cb), methods);
    fflush(stdout);
#endif
    clazz_mcount += methods;
  }
  intf_mcount = 0;
  for (i = 0; i < intf_icount; i++) {
    ClassClass *cb = intf_imtable->itable[i].classdescriptor;
    int methods = cbMethodsCount(cb);
#ifdef FORCE_IMPL_DEBUG
    printf("  intf %s (methods: %d)\n", cbName(cb), methods);
    fflush(stdout);
#endif
    intf_mcount += methods;
  }
  mcount = clazz_mcount + intf_mcount;
#ifdef FORCE_IMPL_DEBUG
  printf("  icount of class, intf: %d, %d\n", clazz_icount, intf_icount);
  printf("  mcount of class, intf: %d, %d\n", clazz_mcount, intf_mcount);
  fflush(stdout);
#endif


  icount = clazz_icount + intf_icount;
  itable_size = offsetof(struct imethodtable, itable) + icount * ITEM_SIZE;
  alloc_size = itable_size + mcount * sizeof(unsigned long);
  alloced = (char *)sysMalloc(alloc_size);

  imtable = (struct imethodtable *)alloced;
  alloced += itable_size;

  imtable->icount = icount;
  memcpy(&imtable->itable[0], &clazz_imtable->itable[0],
		clazz_icount * ITEM_SIZE);	// copy original itable
  memcpy(&imtable->itable[clazz_icount], &intf_imtable->itable[0],
		intf_icount * ITEM_SIZE);	// copy intf's itable

  // copy offsets
  for (i = 0; i < clazz_icount; i++) {
    int nmethod = cbMethodsCount(clazz_imtable->itable[i].classdescriptor);
    unsigned long *clazz_offs = clazz_imtable->itable[i].offsets;
    unsigned long *offs;

    imtable->itable[i].offsets = offs = (unsigned long *)alloced;
    alloced += sizeof(unsigned long) * nmethod;

    for (j = 0; j < nmethod; j++)  offs[j] = clazz_offs[j];
  }
  // make offsets of implemented interface
  for (i = 0; i < intf_icount; i++) {
    int nmethod = cbMethodsCount(intf_imtable->itable[i].classdescriptor);
    unsigned long *intf_offs = intf_imtable->itable[i].offsets;
    unsigned long *offs;

    imtable->itable[clazz_icount +i].offsets = offs = (unsigned long *)alloced;
    alloced += sizeof(unsigned long) * nmethod;

    for (j = 0; j < nmethod; j++)  offs[j] = intf_offs[j];
  }

#if 0	// leak clazz_imtable !!!
  if (clazz_icount) {
    sysFree(clazz_imtable);
  }
#endif
  cbIntfMethodTable(clazz) = imtable;
#ifdef FORCE_IMPL_DEBUG
  {
    int i, limit;
    imtable = cbIntfMethodTable(clazz);
    limit = imtable->icount;
    for (i = 0; i < limit; i++) {
      ClassClass *cb;
      cb = imtable->itable[i].classdescriptor;
      printf("  implements: %s\n", (cb ? cbName(cb) : "null"));
    }
    fflush(stdout);
  }

  printf("forceImplement done.\n", cbName(clazz), cbName(intf));
  fflush(stdout);
#endif
}
