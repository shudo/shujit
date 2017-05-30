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


import java.io.ObjectInputStream;
import java.io.InputStream;
import java.io.IOException;
import java.io.OptionalDataException;
import java.io.StreamCorruptedException;


/**
 * This stream can replace a remote reference
 * with a corresponding local reference.
 */
public class DistObjectInputStream extends ObjectInputStream {
  private boolean debug;

  public DistObjectInputStream(InputStream in)
	throws IOException, StreamCorruptedException {
    super(in);
    this.debug = MetaVM.debug;
    super.enableResolveObject(true);
  }


  ClassLoader classLoader = MetaVM.systemClassLoader;
  /** Set class loader and force this loader to make use of it. */
  public ClassLoader classLoader(ClassLoader cl) {
    ClassLoader orig = this.classLoader;
    this.classLoader = cl;
    return orig;
  }


  /**
   * Read and converts remote objects to local one.
   */
  public Object readDistObject()
	throws OptionalDataException, ClassNotFoundException, IOException {
    boolean orig = MetaVM.remoteTransparency(false);
    Object obj;

//NET.shudo.metavm.VMOperations.printStackTrace();

    obj = this.readObject();

    MetaVM.remoteTransparency(orig);

    return obj;
  }


  protected Object resolveObject(Object obj) throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);
Object origObj = null;
if (debug) {
  origObj = obj;
  System.out.println("resolveObject(): " + obj);
  System.out.flush();
}

    if (obj instanceof Proxy) {		// remte object
      Proxy proxy = (Proxy)obj;
      obj = proxy.localObject();
      if (obj == null)  obj = proxy;
    }
if (debug) {
  if (origObj != obj) {
    System.out.println("  to " + obj);
    System.out.flush();
  }
}

    MetaVM.remoteTransparency(orig);

    return obj;
  }


  protected Class resolveClass(java.io.ObjectStreamClass v)
	throws IOException, ClassNotFoundException {
    Class clazz = null;
    if (classLoader != null) {
      clazz = classLoader.loadClass(v.getName());
    }
    else {
      clazz = Class.forName(v.getName());
    }
    return clazz;
  }
}
