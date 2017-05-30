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

package NET.shudo.metavm;


import java.io.InputStream;
import java.io.FileInputStream;
import java.io.File;
import java.io.IOException;
import java.io.FileNotFoundException;
import java.util.StringTokenizer;
import java.util.zip.ZipFile;
import java.util.zip.ZipEntry;
import java.util.Dictionary;
import java.util.Hashtable;


/**
 * Class loader which loads classes from local disk.
 */
public
class LocalClassLoader extends ClassLoader {
  private static final String COMMAND = "java LocalClassLoader";

  public static void main(String[] argv) {
    new LocalClassLoader().start(argv);
  }

  private void start(String[] argv) {
    if (argv.length < 1) {
      usage(COMMAND);  System.exit(1);
    }

    for (int i = 0; i < argv.length; i++) {
      byte[] classfile = loadClassfileLocally(argv[i]);

      System.out.println(argv[i] + ": " +
	((classfile==null)? "(null)" : Integer.toString(classfile.length)));
    }
  }

  private void usage(String command) {
    System.out.print("usage: ");
    System.out.print(command);
    System.out.println(" classname ...");
  }


  /**
   * An table which contains classes is already loaded.
   */
  private Dictionary cache = new Hashtable();

  /**
   * implements java.lang.ClassLoader#loadClass().
   */
  public synchronized Class loadClass(String name, boolean resolve)
	throws ClassNotFoundException {
//System.out.println("LocalClassLoader#loadClass(" + name + ") is called.");
//System.out.flush();
    Class c = null;
    c = (Class)cache.get(name);
    if (c == null) {
      try {
	c = findSystemClass(name);
//System.out.println("findSystemClass() returns: " + c);
//System.out.flush();
      }
      catch (ClassNotFoundException e) {
	byte data[] = loadClassfileLocally(name);
	if (data == null)  throw new ClassNotFoundException(name);
	c = defineClass(name, data, 0, data.length);
      }

      if (c != null)
	cache.put(name, c);
    }
    if (resolve)
      resolveClass(c);
    return c;
  }


  /**
   * Find classfile on CLASSPATH.
   *
   * @return byte array contains classfile
   */
  public static byte[] loadClassfileLocally(String classname) {
    String classnamePath =
	classname.replace('.', File.separatorChar) + ".class";
    String classPath = null;
    StringTokenizer tokenizer = null;
    byte[] ret;

    classPath = System.getProperty("java.class.path");
    if (classPath == null)  return null;
    tokenizer = new StringTokenizer(classPath, File.pathSeparator);

    while (tokenizer.hasMoreTokens()) {
      String aPath = tokenizer.nextToken();
      File f = new File(aPath);
      if (!f.exists())  continue;

      if (f.isDirectory()) {
	// supply `/' or `\' to the tail of the path.
	if (!aPath.endsWith(File.separator)) {
	  aPath += File.separator;
	  f = new File(aPath);
	}

	File classfileFile = new File(aPath + classnamePath);
	if (classfileFile.exists() && classfileFile.isFile()) {
	  long fileLength = classfileFile.length();
	  if (fileLength > (long)Integer.MAX_VALUE)
	    fileLength = (long)Integer.MAX_VALUE;
	  ret = new byte[(int)fileLength];

	  FileInputStream fin = null;
	  try {
	    fin = new FileInputStream(classfileFile);
	  }
	  catch (FileNotFoundException e) {
	    // not reached.
	    continue;
	  }

	  try {
	    int remain = ret.length;
	    while (remain > 0) {
	      remain -= fin.read(ret, ret.length - remain, remain);
	    }
	  }
	  catch (IOException e) {
	    continue;
	  }

	  // check magic number
	  if (ret[0] == (byte)0xca && ret[1] == (byte)0xfe &&
		ret[2] == (byte)0xba && ret[3] == (byte)0xbe)
	    return ret;
	  else
	    continue;
	}
      }

      if (f.isFile()) {
	ZipFile archive = null;
	ZipEntry entry = null;
	int size;
	try {
	  archive = new ZipFile(f);
	}
	catch (IOException e) {
	  continue;
	}

	entry = archive.getEntry(classnamePath);
	if (entry == null)  continue;
	long fileLength = entry.getSize();
	if (fileLength < 0)  continue;
	if (fileLength > (long)Integer.MAX_VALUE)
	  fileLength = (long)Integer.MAX_VALUE;
	ret = new byte[(int)fileLength];

	InputStream in = null;
	try {
	  in = archive.getInputStream(entry);

	  int remain = ret.length;
	  while (remain > 0) {
	    remain -= in.read(ret, ret.length - remain, remain);
	  }
	}
	catch (IOException e) {
	  // not reached.
	  continue;
	}

	return ret;
      }
    }

    return null;
  }
}
