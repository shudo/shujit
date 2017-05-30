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


import java.util.Dictionary;
import java.util.Hashtable;
import java.net.Socket;
import java.net.InetAddress;
import java.io.IOException;
import java.net.UnknownHostException;
import java.io.BufferedOutputStream;
import java.io.BufferedInputStream;
import java.io.DataOutputStream;
import java.io.DataInputStream;
import java.util.Enumeration;


/**
 * Class loader which loads classes from remote machine.
 * Classfiles is supplied by remote ClassDistributor.
 *
 * @see NET.shudo.metavm.ClassDistributor
 */
public final
class RemoteClassLoader extends ClassLoader {
  /**
   * Table of RemoteClassLoader. The key is VMAddress.
   */
  private static Dictionary classLoaderTable = new Hashtable();

  private boolean debug = MetaVM.debug;

  private boolean alwaysLoadNotStandardClassesViaNetwork = true;

  /**
   * 接続先、つまりクラスロード元のアドレス
   */
  private VMAddress sourceAddress;

  /**
   * クラスサーバへのコネクション
   */
  private Socket sock = null;

  private DataInputStream in = null;
  DataOutputStream out = null;


  private RemoteClassLoader(VMAddress addr) throws IOException {
    this.init(addr);
  }


  protected static void reset() {
    synchronized(classLoaderTable) {
      // disconnect all sockets
      Enumeration en = classLoaderTable.elements();
      while (en.hasMoreElements()) {
	RemoteClassLoader cl = (RemoteClassLoader)(en.nextElement());
	try {
	  cl.disconnect();
	}
	catch (IOException e) { /* ignore */ }
      }

      // re-create ClassLoader table
      classLoaderTable = new Hashtable();
    }
  }


  public static RemoteClassLoader get(VMAddress addr) throws IOException {
    RemoteClassLoader cl = null;

    synchronized (classLoaderTable) {
      cl = (RemoteClassLoader)classLoaderTable.get(addr);
      if (cl == null) {
	cl = new RemoteClassLoader(addr);
	classLoaderTable.put(addr, cl);
      }
      else {
	if (cl.sock == null)  cl.init(addr);
      }
    }

    return cl;
  }


  /**
   * Initialization.
   */
  private synchronized void init(VMAddress addr) throws IOException {
    this.sourceAddress = addr;
    connect();
  }

  private void connect() throws IOException {
    if (sourceAddress == null)
      throw new IOException("source address is not specified.");

    sock = new Socket(sourceAddress.inetAddress(),
		sourceAddress.port() + VMAddress.CLASSLOADER_PORT_DIFF);
    out = new DataOutputStream(new BufferedOutputStream(
					sock.getOutputStream()));
    in = new DataInputStream(new BufferedInputStream(sock.getInputStream()));
  }

  private void disconnect() throws IOException {
    try {
      sock.close();
    }
    catch (IOException e) { /* ignore */ }

    sock = null; out = null; in = null;
  }


  /**
   * Returns the address where class distributor stays.
   */
  public VMAddress sourceAddress() { return this.sourceAddress; }


  /**
   * Implements java.lang.ClassLoader#loadClass().
   */
  public synchronized Class loadClass(String name, boolean resolve)
	throws ClassNotFoundException {
    Class c = null;
    boolean done = false;

    if (debug) {
      System.out.println("RemoteClassLoader#loadClass() classname: " + name);
      System.out.flush();
    }

    // initialize if this loader is disabled.
    if (sock == null) {
      try { this.connect(); }
      catch (IOException e) {
	throw new ClassNotFoundException(
		"initialization of class loader failed.");
      }
    }

    if ((name == null) || (name.length() == 0))
      throw new ClassNotFoundException("class name is not specified.");

    c = findLoadedClass(name);
    if (c == null) {
      if ( (!alwaysLoadNotStandardClassesViaNetwork) ||
	name.startsWith("sun.") || name.startsWith("java.") ||
	name.startsWith("[") || name.startsWith("NET.shudo.metavm.") ) {

	if (debug) {
	  System.out.println("try to load from local disk.");
	  System.out.flush();
	}

	// delegate to system class loader
	try {
	  c = findSystemClass(name);
	  done = true;
	}
	catch (ClassNotFoundException e) {}
      }

      if (!done) {
	if (debug) {
	  System.out.println("try to load from network.");
	  System.out.flush();
	}

	if (sock == null) {
	  throw new ClassNotFoundException(
		"this classloader is already disabled.");
	}

	byte[] data = null;
	int retryCount = 0;
	do {
	  try {
	    out.writeUTF(name);
	    out.flush();

	    int dataLength = in.readInt();
	    if (debug) {
	      System.out.println("length: " + dataLength);  System.out.flush();
	    }
	    if (dataLength <= 0)  throw new ClassNotFoundException(name);
	    data = new byte[dataLength];

	    int remain = data.length;
	    while (remain > 0) {
	      remain -= in.read(data, data.length - remain, remain);
	    }

	    done = true;
	  }
	  catch (IOException e) {
	    if (retryCount > 0)
	      throw new ClassNotFoundException(name);

	    // re-initialize
	    if (debug) {
	      System.out.println("RemoteClass Loader: try to re-initialize.");
	    }
	    retryCount++;
	    try {  this.connect();  }
	    catch (IOException initEx) {
	      throw new ClassNotFoundException(name);
	    }
	  }
	} while (!done);

	c = defineClass(name, data, 0, data.length);
      }
    }	// if (c == null)

    if (resolve)
      resolveClass(c);

    if (debug) {
      System.out.println("RemoteClassLoader#loadClass() done.");
      System.out.flush();
    }
    return c;
  }
}
