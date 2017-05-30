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

package NET.shudo.metavm;


public class VMOperations {
  private VMOperations() {}	// indicates prohibition of instantiation

  public static native Object instantiate(Class clazz);
  public static native Object newarray(int type, int count);
	/* type is T_* in typecodes.h */
  public static native Object anewarray(Class clazz, int count);
  public static native Object multianewarray(Class aryclz, int[] sizes);

  public static native int get32Field(Object obj, int slot);
  public static native long get64Field(Object obj, int slot);
  public static native Object getObjectField(Object obj, int slot);
  public static native void put32Field(Object obj, int slot, int val);
  public static native void put64Field(Object obj, int slot, long val);
  public static native void putObjectField(Object obj, int slot, Object val);

  public static native Object invoke(Object receiver,
	String name, String sig, Object[] args) throws Throwable;
  public static native Object invoke(Object receiver,
	int slot, int mbindex, Object[] args)
		throws Throwable;

  public static native void monitorEnter(Object obj);
  public static native void monitorExit(Object obj);


  public static native boolean isNativeMethod(Class clazz,
						int slot, int mbindex);
  public static native String MethodName(Class clazz,
						int slot, int mbindex);
  public static native String MethodSignature(Class clazz,
						int slot, int mbindex);


  public static native void printStackTrace();
}
