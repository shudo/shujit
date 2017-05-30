/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 2003 Kazuyuki Shudo

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


typedef struct {
  char* name;
  void* p;
} LibraryEntry;


// function table
//   The static linked binary for Xbox uses this table
//   to resolve symbols dynamically.

LibraryEntry shujitFuncTable[] = {
  // JNI_OnLoad Entry
  //{JNI_ONLOAD, dxglu_JNI_OnLoad},

    /* JAVA JNI FUNCTIONS BEGIN */
  {"compileAndInvokeMethod", compileAndInvokeMethod},
  {"invokeJITCompiledMethod", invokeJITCompiledMethod},
  {"invokeJavaMethod", invokeJavaMethod},
  {"invokeSynchronizedJavaMethod", invokeSynchronizedJavaMethod},
  {"invokeAbstractMethod", invokeAbstractMethod},
  {"invokeNativeMethod", invokeNativeMethod},
  {"invokeSynchronizedNativeMethod", invokeSynchronizedNativeMethod},
  {"invokeJNINativeMethod", invokeJNINativeMethod},
  {"invokeJNISynchronizedNativeMethod", invokeJNISynchronizedNativeMethod},
  {"invokeLazyNativeMethod", invokeLazyNativeMethod},
  //{"java_lang_Compiler_start", java_lang_Compiler_start},
    /* JAVA JNI FUNCTIONS END */
  {NULL, NULL}
};
