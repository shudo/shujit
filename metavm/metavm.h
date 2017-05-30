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

#ifndef _METAVM_H_
#define _METAVM_H_

#include "../config.h"

#include "NET_shudo_metavm_VMAddress_old.h"	// for proxy_new()
#include "native.h"


//
// MetaVM specific macro definitions
//

#define METAVM_PKG	"NET/shudo/metavm/"

#if JDK_VER >= 12
#  define GET_REMOTE_FLAG(EE) \
	(*(((char *)(EE)) + 17))
#  define SET_REMOTE_FLAG(EE, FLAG) \
	(*(((char *)(EE)) + 17) = (char)(FLAG))
#  define REMOTE_ADDR(EE)	((JHandle *)(EE)->RESERVED3)
//#  define REMOTE_ADDR(EE)	((JHandle *)(EE)->exception.exc)
#else	// JDK_VER
#  define GET_REMOTE_FLAG(EE) \
	((EE)->alloc_cache.cache_pad[0])
#  define SET_REMOTE_FLAG(EE, FLAG) \
	((EE)->alloc_cache.cache_pad[0] = (char)(FLAG))
#  define REMOTE_ADDR(EE)	((JHandle *)(EE)->exception.exc)
#endif	// JDK_VER

#define REMOTE_FLAG_OFF(EE)	SET_REMOTE_FLAG(EE, 0)
#define REMOTE_FLAG_ON(EE)	SET_REMOTE_FLAG(EE, 1)


#if JDK_VER >= 12
#  define MK_REF_LOCAL(env, jobj) \
	MkRefLocal(env, jobj)
#else
#  define MK_REF_LOCAL(env, jobj) \
	((jobject)MkRefLocal(env, jobj, JNI_REF_HANDLE_TAG))
#endif


//
// Global variables
//
// proxy.c
extern ClassClass *cb_Boolean;
extern ClassClass *cb_Byte;
extern ClassClass *cb_Character;
extern ClassClass *cb_Short;
extern ClassClass *cb_Integer;
extern ClassClass *cb_Long;
extern ClassClass *cb_Float;
extern ClassClass *cb_Double;

extern ClassClass *cb_ArrayOfObject;

extern ClassClass *cb_Proxy;


//
// Global functions
//
// type.c
extern int isCheckPassType(ClassClass *cb);
extern void forceToImplement(ExecEnv *ee, ClassClass *clazz, ClassClass *intf);

// byvalue.c
extern bool_t isByValue(ExecEnv *ee, ClassClass *clazz);

// proxy.c
extern JHandle *proxy_new(ExecEnv *, ClassClass *,
	HNET_shudo_metavm_VMAddress *, ClassClass *);
extern JHandle *proxy_newarray(ExecEnv *, ClassClass *,
	HNET_shudo_metavm_VMAddress *, int type, int count);
extern JHandle *proxy_anewarray(ExecEnv *, ClassClass *,
	HNET_shudo_metavm_VMAddress *, ClassClass *, int count);
extern JHandle *proxy_multianewarray(ExecEnv *, ClassClass *,
	HNET_shudo_metavm_VMAddress *, ClassClass *,
	int dim, stack_item *stackpointer);

#define DECL_GET_FIELD(NAME, CTYPE) \
	extern CTYPE proxy_##NAME(ExecEnv *, JHandle *proxy,\
					... /*int32_t slot*/)
DECL_GET_FIELD(get32field, jint);
DECL_GET_FIELD(get64field, jlong);
DECL_GET_FIELD(getobjfield, JHandle *);
DECL_GET_FIELD(aload32, jint);
DECL_GET_FIELD(aload64, jlong);
DECL_GET_FIELD(aloadobj, JHandle *);
#define DECL_PUT_FIELD(NAME, CTYPE) \
	extern void proxy_##NAME(ExecEnv *, JHandle *proxy, int32_t, CTYPE)
DECL_PUT_FIELD(put32field, int32_t);
DECL_PUT_FIELD(put64field, int64_t);
DECL_PUT_FIELD(putobjfield, JHandle *);
DECL_PUT_FIELD(astore32, int32_t);
DECL_PUT_FIELD(astore64, int64_t);
DECL_PUT_FIELD(astoreobj, JHandle *);

extern int32_t proxy_arraylength(ExecEnv *ee, JHandle *proxy);
extern int proxy_invoke(ExecEnv *, JHandle *proxy,
	struct methodblock *mb, stack_item *stackpointer);
extern int proxy_monitorenter(ExecEnv *, JHandle *proxy);
extern int proxy_monitorexit(ExecEnv *, JHandle *proxy);
extern JHandle *proxy_getObjCopy(ExecEnv *, JHandle *proxy);
extern JHandle *proxy_getArrayCopy(ExecEnv *, JHandle *proxy,
					int32_t position, int32_t length);
extern void proxy_aryBlockLoad(ExecEnv *, JHandle *proxy,
	       JHandle *dst, int32_t srcpos, int32_t len, int32_t dstpos);

#endif	// _METAVM_H_
