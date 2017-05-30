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

package NET.shudo.metavm;

import java.util.Dictionary;
import java.util.Hashtable;


public class TypeUtil {
  private TypeUtil() {}	// indicates prohibition of instantiation


  // types corresponded with argument of JVM op. `newarray'
  public static final int T_OBJECT	= 0;
  public static final int T_BOOLEAN	= 4;
  public static final int T_CHAR	= 5;
  public static final int T_FLOAT	= 6;
  public static final int T_DOUBLE	= 7;
  public static final int T_BYTE	= 8;
  public static final int T_SHORT	= 9;
  public static final int T_INT		= 10;
  public static final int T_LONG	= 11;
  public static final int T_MAX		= 11;


  private static Dictionary primTableFullToShort;
  private static Dictionary primTableShortToClass;
  static {
    primTableFullToShort = new Hashtable();
    primTableFullToShort.put("void", "V");
    primTableFullToShort.put("boolean", "Z");
    primTableFullToShort.put("byte", "B");
    primTableFullToShort.put("char", "C");
    primTableFullToShort.put("short", "S");
    primTableFullToShort.put("int", "I");
    primTableFullToShort.put("long", "J");
    primTableFullToShort.put("float", "F");
    primTableFullToShort.put("double", "D");

    primTableShortToClass = new Hashtable();
    primTableShortToClass.put("Z", boolean.class);
    primTableShortToClass.put("B", byte.class);
    primTableShortToClass.put("C", char.class);
    primTableShortToClass.put("S", short.class);
    primTableShortToClass.put("I", int.class);
    primTableShortToClass.put("J", long.class);
    primTableShortToClass.put("F", float.class);
    primTableShortToClass.put("D", double.class);
  }

  private static Class[] primTypeByCode;
  static {
    primTypeByCode = new Class[T_MAX + 1];
    primTypeByCode[T_BOOLEAN] = boolean.class;
    primTypeByCode[T_CHAR] = char.class;
    primTypeByCode[T_FLOAT] = float.class;
    primTypeByCode[T_DOUBLE] = double.class;
    primTypeByCode[T_BYTE] = byte.class;
    primTypeByCode[T_SHORT] = short.class;
    primTypeByCode[T_INT] = int.class;
    primTypeByCode[T_LONG] = long.class;
  }


  public static Class primTypeByCode(int tcode) {
    Class ret = null;

    try {
      ret = primTypeByCode[tcode];
    }
    catch (ArrayIndexOutOfBoundsException e) {
      // not reached.
      System.err.println("type code is invalid: " + tcode);
    }

    return ret;
  }


  public static Class arrayType(Class clazz) {
    ClassLoader cl = clazz.getClassLoader();
    String name = clazz.getName();
    String componentName = null;
    String aryClazzName = null;
    Class ret;

    if (name.startsWith("["))  componentName = name;
    else {
      componentName = (String)primTableFullToShort.get(name);
      if (componentName == null) {
	componentName = "L" + name + ";";
      }
    }
    aryClazzName = "[" + componentName;

    try {
      if (cl == null)
	ret = Class.forName(aryClazzName);
      else
	ret = cl.loadClass(aryClazzName);
    }
    catch (ClassNotFoundException e) {
      // not reached.
      System.err.println("class object of array of `" + name +
				"' cannot be obtained.");
      ret = null;
    }

    return ret;
  }


  public static Class componentType(Class arrayClazz) {
    ClassLoader cl = arrayClazz.getClassLoader();
    String name = arrayClazz.getName();
    String componentName;
    Class ret;

    if (!name.startsWith("["))  return null;

    componentName = name.substring(1);
    if (!componentName.startsWith("L")) {	// primitive type
      ret = (Class)primTableShortToClass.get(componentName);
    }
    else {
      try {
	if (cl == null)
	  ret = Class.forName(componentName);
	else
	  ret = cl.loadClass(componentName);
      }
      catch (ClassNotFoundException e) {
	// not reached.
	System.err.println("class object of component type of `" + name +
				"' cannot be obtained.");
	ret = null;
      }
    }

    return ret;
  }


  // these methods must be protected, now testing...
  synchronized public static native void addCheckPassType(Class clazz);
  synchronized public static native void clearCheckPassType();

  public static void forceToImplement(Class clazz, Class intf) {
    if (intf.isAssignableFrom(clazz))  return;
    forceToImplement0(clazz, intf);
  }
  private static native void forceToImplement0(Class clazz, Class intf);


  // for test
  public static void main(String[] args) {
    Class clazz = null;

    clazz = int.class;
    clazz = arrayType(clazz);
    System.out.println(clazz.getName());

    clazz = int[].class;
    clazz = arrayType(clazz);
    System.out.println(clazz.getName());

    clazz = java.lang.Object.class;
    clazz = arrayType(clazz);
    System.out.println(clazz.getName());

    try {
      clazz = Class.forName("[Ljava.lang.Object;");
	// This is array class, so load from local disk.
    }
    catch (ClassNotFoundException e) {
      e.printStackTrace();
    }
    clazz = arrayType(clazz);
    System.out.println(clazz.getName());
  }
}
