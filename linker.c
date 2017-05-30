/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1999,2000,2002,2003 Kazuyuki Shudo

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

#include "java_util_Vector.h"
#include "java_lang_ClassLoader_NativeLibrary.h"

#ifdef _WIN32
#  include <limits.h>	// for PATH_MAX
#endif


#if JDK_VER >= 12
void *symbolInSystemClassLoader(char *name) {
  static Hjava_util_Vector *sysNativeLibs = NULL;
  static void *java_handle = NULL;
  static void *jvm_handle = NULL;
  static void *jit_handle = NULL;

  void *ret;
  int i;

  if (!sysNativeLibs) {
    ExecEnv *ee = EE();
    ClassClass *cb = classJavaLangClassLoader;
    HashedNameAndType hashed;
    struct fieldblock *fb;
    OBJECT *slot;

    HashNameAndType(ee,
	"systemNativeLibraries", "Ljava/util/Vector;", &hashed);

    for (i = cbFieldsCount(cb) - 1, fb = cbFields(cb); i >= 0; i--, fb++) {
      if (NAMETYPE_MATCH(&hashed, fb))  break;
    }
    if (i < 0)  goto symsys_error;

    slot = (OBJECT*)(normal_static_address(fb));
    sysNativeLibs = (Hjava_util_Vector *)*slot;
  }

  {
    static HArrayOfObject *sysNativeArray;
    HObject **body;
    int32_t count;
    void *handle;

    sysNativeArray = unhand(sysNativeLibs)->elementData;
    body = unhand(sysNativeArray)->body;
    count = unhand(sysNativeLibs)->elementCount;

    for (i = 0; i < count; i++) {
      Hjava_lang_ClassLoader_NativeLibrary *nativeLib;

      if (!(nativeLib = (Hjava_lang_ClassLoader_NativeLibrary *)body[i]))
	continue;
      handle = (void *)(int32_t)(unhand(nativeLib)->handle);
		// jlong_to_ptr ()

      if ((ret = JVM_FindLibraryEntry(handle, name)) != NULL)
	return ret;
    }
  }

  {
    char buf[PATH_MAX + 1];
    char load_error[STK_BUF_LEN*2];

#ifdef DEBUG
#  define LIB_SUFFIX	"_g.so"
#else
#  define LIB_SUFFIX	".so"
#endif

#define SEARCH_LIB(LIB_NAME, DIR_NAME, VAR_NAME) \
    if (!VAR_NAME) {\
      /* sysBuildLibName(buf, sizeof(buf), java_dll_dir, LIB_NAME); */\
      snprintf(buf, sizeof(buf), "%s/%slib%s" LIB_SUFFIX,\
		java_dll_dir, DIR_NAME, LIB_NAME);\
      VAR_NAME = sysLoadLibrary(buf, load_error, sizeof(load_error));\
      if (!VAR_NAME) {\
	fprintf(stderr, "FATAL: can't load library %s, %s\n",\
		buf, load_error);\
	goto symsys_error;\
      }\
    }\
    if ((ret = JVM_FindLibraryEntry(VAR_NAME, name)) != NULL)\
      return ret;

    SEARCH_LIB("java", "", java_handle);
    SEARCH_LIB(JIT_LIB_NAME, "", jit_handle);
#if JDK_VER >= 12
    SEARCH_LIB("jvm", "classic/", jvm_handle);
	// FreeBSD always need search through libjvm.so.
	// Linux needs this only for Java Plugin.
#endif
  }

  return NULL;

symsys_error:
  /* NOTREACHED */
  printf("FATAL: symbolInSystemClassLoader()\n");
  fflush(stdout);
  JVM_Exit(1);
}
#endif	// JDK_VER
