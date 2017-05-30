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

#include "jni.h"
#include "native.h"


#define BYVALUEUTIL_CLASSNAME	METAVM_PKG "ByValueUtil"


bool_t isByValue(ExecEnv *ee, ClassClass *clazz) {
  static ClassClass *cb_ByValueUtil = NULL;
  static struct methodblock *mb_isByValue = NULL;

  bool_t result;
#ifdef RUNTIME_DEBUG
  printf("isByValue(0x%08x, 0x%08x) called: ", (int)ee, (int)clazz);
  fflush(stdout);
  printf("%s\n", (clazz ? cbName(clazz) : "null"));
  fflush(stdout);
#endif

  if (mb_isByValue == NULL) {
    int i;
    struct methodblock *mb;
#if JDK_VER >= 12
    HashedNameAndType hashed;
#else
    unsigned hashed;
#endif

    cb_ByValueUtil = FindClass(ee, BYVALUEUTIL_CLASSNAME, TRUE);
#if 0
    if (cb_ByValueUtil == NULL) {
      /* NOTREACHED */
      printf("FATAL: class \"" BYVALUEUTIL_CLASSNAME "\" is not found.\n");
    }
#endif
#if JDK_VER >= 12
    HashNameAndType(ee, "isByValue", "(Ljava/lang/Class;)Z", &hashed);
#else
    hashed = NameAndTypeToHash("isByValue", "(Ljava/lang/Class;)Z");
#endif
    for (i = cbMethodsCount(cb_ByValueUtil) - 1; i >= 0; i--) {
      mb = &(cbMethods(cb_ByValueUtil)[i]);
#if JDK_VER >= 12
      if (NAMETYPE_MATCH(&hashed, &(mb->fb)))  goto mb_found;
#else
      if (mb->fb.ID == hashed)  goto mb_found;
#endif
    }
    fprintf(stderr, "FATAL: cannot find isByValue() method.\n");
    JVM_Exit(1);
  mb_found:
    mb_isByValue = mb;
  }

  result = (bool_t)do_execute_java_method(ee, cb_ByValueUtil,
	"isByValue", "(Ljava/lang/Class;)Z",
	mb_isByValue,	// struct methodblock *
	TRUE,		// isStaticCall
	clazz);

#ifdef RUNTIME_DEBUG
  printf("isByValue() returns: %s\n", (result?"true":"false"));
  fflush(stdout);
#endif
  return result;
}
