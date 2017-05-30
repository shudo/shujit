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

import java.io.Serializable;
import java.net.InetAddress;
import java.net.Socket;
import java.io.IOException;
import java.io.OutputStream;
import java.io.InputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedInputStream;
import java.util.Dictionary;
import java.util.Hashtable;
import java.lang.ref.WeakReference;
import NET.shudo.metavm.registry.Registry;


/**
 * An instance of this class represents a reference to remote object.
 */
public class Proxy implements ByValue, Cloneable {
  private VMAddress vmaddr;	// where the remote object is
  private VMAddress clzsrc;	// from where the class was loaded

  private Class clz;
  private int id;
  private int length;

  transient private Socket sock = null;
  transient private DistObjectOutputStream out;
  transient private DistObjectInputStream in;

  /** debug flag. */
  transient private boolean debug = false;


  static {
    initNative();
  }
  private static native void initNative();

  private static Dictionary proxyTable = new Hashtable();


  protected Object clone() throws CloneNotSupportedException {
    return newInstance(vmaddr, clz, id, length);
  }


  /**
   * Retuns VM address where this proxy stays on.
   */
  protected VMAddress address() { return vmaddr; }


  private static Proxy newInstance(VMAddress addr,
				Class clazz, int id, int length) {
    Proxy ret = new Proxy(addr, clazz, id, length);

    // interning
    //   - but not perfect because Proxy#hashCode() may conflicts.
    WeakReference existingRef = (WeakReference)proxyTable.get(ret);
    Proxy existingObj;
    if (existingRef != null) {
      existingObj = (Proxy)existingRef.get();
      if (existingObj != null) {
	return existingObj;
      }
    }

    if (id != -1)	// id is valid
      proxyTable.put(ret, new WeakReference(ret));

    return ret;
  }
  private static Proxy newInstance(VMAddress addr, Class clazz) {
    return newInstance(addr, clazz, -1, 0);
  }
  private static Proxy newInstance(VMAddress addr) {
    return newInstance(addr, null, -1, 0);
  }
  private Proxy(VMAddress addr, Class clazz, int id, int length) {
    this.vmaddr = addr;

    this.clzsrc = VMAddress.localAddress();
    if (clazz != null) {
      ClassLoader cl = clazz.getClassLoader();
      if ((cl != null) && (cl instanceof RemoteClassLoader))
	this.clzsrc = ((RemoteClassLoader)cl).sourceAddress();
		// class should be loaded from the same source as `clazz'.
}

    this.clz = clazz;
    this.id = id;
    this.length = length;

    this.debug = MetaVM.debug;
  }


  public int hashCode() {	// to be improved
    return this.vmaddr.hashCode() + id;
  }

  public boolean equals(Object obj) {
    if (obj instanceof Proxy) {
      Proxy prox = (Proxy)obj;
      if (this.vmaddr.equals(prox.vmaddr) && (this.id == prox.id))
	return true;
    }
    return false;
  }


  /**
   * Return a proxy object corresponding to the supplied local object.
   */
  protected static Proxy get(Object obj) {
    Proxy proxy;
    int length = 0;
    boolean orig = MetaVM.remoteTransparency(false);

    if (obj.getClass().isArray()) {
      length = java.lang.reflect.Array.getLength(obj);
    }

    proxy = newInstance(VMAddress.localAddress(),
			obj.getClass(), ObjectID.idByObject(obj), length);
    if (MetaVM.debug) {
      System.out.println("  vmaddr: " + proxy.vmaddr);
      System.out.println("  obj: " + obj);
      System.out.println("  id : " + proxy.id);
    }

    ExportTable.register(obj);

    MetaVM.remoteTransparency(orig);

    return proxy;
  }

  /**
   * bytecode insn. `new'.
   */
  protected static Proxy get(Class fromClazz, VMAddress addr, Class clazz)
	throws IOException {
		// don't make use of formClazz now.
    if ((addr == null) || (clazz == null))
      throw new IOException("VM address and class must be specified.");

    Proxy proxy;  int id;
    DistObjectOutputStream out;  DistObjectInputStream in;
    boolean orig = MetaVM.remoteTransparency(false);

    proxy = newInstance(addr, clazz);
    proxy.establishConnection();

    out = proxy.out;  in = proxy.in;

    out.writeByte(Protocol.NEW);

    VMAddress classsrc = null;
    ClassLoader cl = clazz.getClassLoader();
    if ((cl != null) && (cl instanceof RemoteClassLoader))
      classsrc = ((RemoteClassLoader)cl).sourceAddress();
    else
      classsrc = VMAddress.localAddress();

    if (MetaVM.debug) {
      System.out.println("Proxy.get() classsrc is " + classsrc + ".");
      System.out.flush();
    }

    out.writeObject(classsrc);
    out.writeUTF(clazz.getName());
    out.flush();

    id = in.readInt();
    if (id == 0) {
      IOException t =
	new IOException("instantiation failure: " + clazz.getName());
      MetaVM.remoteTransparency(orig);
      throw t;
    }
    proxy.id = id;
    proxyTable.put(proxy, new WeakReference(proxy));

    MetaVM.remoteTransparency(orig);

    return proxy;
  }

  /**
   * bytecode insn. `newarray'.
   */
  protected static Proxy get(Class fromClazz, VMAddress addr, int type, int count)
	throws IOException {
    Proxy proxy;  int id;
    DistObjectOutputStream out;  DistObjectInputStream in;
    boolean orig = MetaVM.remoteTransparency(false);

    proxy = newInstance(addr);
    proxy.establishConnection();

    out = proxy.out;  in = proxy.in;

    out.writeByte(Protocol.NEWARRAY);

    VMAddress classsrc = null;
    ClassLoader cl = fromClazz.getClassLoader();
    if ((cl != null) && (cl instanceof RemoteClassLoader))
      classsrc = ((RemoteClassLoader)cl).sourceAddress();
    else
      classsrc = VMAddress.localAddress();

    if (MetaVM.debug) {
      System.out.println("Proxy.get() classsrc is " + classsrc + ".");
      System.out.flush();
    }

    out.writeObject(classsrc);
    out.writeByte(type);
    out.writeInt(count);
    out.flush();

    proxy.clz = TypeUtil.arrayType(TypeUtil.primTypeByCode(type));

    id = in.readInt();
    if (id == 0) {
      IOException t =
	new IOException("instantiation failure: " + proxy.clz.getName());
      MetaVM.remoteTransparency(orig);
      throw t;
    }
    proxy.id = id;
    proxyTable.put(proxy, new WeakReference(proxy));
    proxy.length = count;

    MetaVM.remoteTransparency(orig);

    return proxy;
  }

  /**
   * bytecode insn. `anewarray'.
   */
  protected static Proxy get(Class fromClazz, VMAddress addr,
				Class clazz, int count) throws IOException {
    Proxy proxy;  int id;
    DistObjectOutputStream out;  DistObjectInputStream in;
    boolean orig = MetaVM.remoteTransparency(false);

    proxy = newInstance(addr);
    proxy.establishConnection();

    out = proxy.out;  in = proxy.in;

    out.writeByte(Protocol.ANEWARRAY);

    VMAddress classsrc = null;
    ClassLoader cl = fromClazz.getClassLoader();
    if ((cl != null) && (cl instanceof RemoteClassLoader))
      classsrc = ((RemoteClassLoader)cl).sourceAddress();
    else
      classsrc = VMAddress.localAddress();

    if (MetaVM.debug) {
      System.out.println("Proxy.get() classsrc is " + classsrc + ".");
      System.out.flush();
    }

    out.writeObject(classsrc);
    out.writeUTF(clazz.getName());
    out.writeInt(count);
    out.flush();

    proxy.clz = TypeUtil.arrayType(clazz);

    id = in.readInt();
    if (id == 0) {
      IOException t =
	new IOException("instantiation failure: " + clazz.getName());
      MetaVM.remoteTransparency(orig);
      throw t;
    }
    proxy.id = id;
    proxyTable.put(proxy, new WeakReference(proxy));
    proxy.length = count;

    MetaVM.remoteTransparency(orig);

    return proxy;
  }

  /**
   * bytecode insn. `multianewarray'.
   */
  protected static Proxy get(Class fromClazz, VMAddress addr,
			Class clazz, int[] sizes) throws IOException {
    Proxy proxy;  int id;
    DistObjectOutputStream out;  DistObjectInputStream in;
    boolean orig = MetaVM.remoteTransparency(false);

    proxy = newInstance(addr);
    proxy.establishConnection();

    out = proxy.out;  in = proxy.in;

    out.writeByte(Protocol.MULTIANEWARRAY);

    VMAddress classsrc = null;
    ClassLoader cl = fromClazz.getClassLoader();
    if ((cl != null) && (cl instanceof RemoteClassLoader))
      classsrc = ((RemoteClassLoader)cl).sourceAddress();
    else
      classsrc = VMAddress.localAddress();

    out.writeObject(classsrc);
    out.writeUTF(clazz.getName());
    out.writeDistObject(new ByValueWrapper(sizes));
    out.flush();

    proxy.clz = TypeUtil.arrayType(clazz);

    id = in.readInt();
    if (id == 0) {
      IOException t =
	new IOException("instantiation failure: " + clazz.getName());
      MetaVM.remoteTransparency(orig);
      throw t;
    }
    proxy.id = id;
    proxyTable.put(proxy, new WeakReference(proxy));
    proxy.length = sizes[0];

    MetaVM.remoteTransparency(orig);

    return proxy;
  }


  /**
   * Return a proxy object corresponding to the registry
   * on a specified machine.
   */
  protected static Registry getRegistry(VMAddress addr) throws IOException {
    Proxy proxy;
    Object obj;
    boolean orig = MetaVM.remoteTransparency(false);

    proxy = newInstance(addr);
    proxy.referRegistry();

    obj = proxy;

    MetaVM.remoteTransparency(orig);

    return (Registry)obj;
  }


  /**
   * Get a reference to a remote object
   * correspoinding to this proxy object.
   */
  private void reference() throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    establishConnection();

    out.writeByte(Protocol.REFERENCE);
    out.writeInt(id);
    out.flush();

    try {
      reference0();
    }
    finally {
      MetaVM.remoteTransparency(orig);
    }
  }


  /**
   * Get a reference to the registry on a remote machine.
   */
  private void referRegistry() throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    establishConnection();

    out.writeByte(Protocol.REFREGISTRY);
    out.flush();

    try {
      reference0();
    }
    finally {
      MetaVM.remoteTransparency(orig);
    }
  }


  private void reference0() throws IOException {
    if (in.readByte() != Protocol.OK) {
      throw new IOException("couldn't get reference with id " + id);
    }

    VMAddress classsrc = null;
    try {
      classsrc = (VMAddress)in.readObject();
    }
    catch (ClassNotFoundException e) {
      // not reached
      boolean orig = MetaVM.remoteTransparency(false);
      if (debug)  e.printStackTrace();
      MetaVM.remoteTransparency(orig);
    }

    String classname = in.readUTF();
    try {
      ClassLoader cl;
      if (MetaVM.load_class_locally || classsrc.isLocalAddress()) {
	// load the class from local disk
	cl = MetaVM.systemClassLoader;
      }
      else {
	// load via network
	cl = RemoteClassLoader.get(classsrc);
      }
      in.classLoader(cl);	// set class loader

      this.clz = cl.loadClass(classname);
    }
    catch (ClassNotFoundException e) {
      throw new IOException("class not found: " + e.getMessage());
    }

    this.id = in.readInt();
    proxyTable.put(this, new WeakReference(this));

    this.length = in.readInt();
  }


  /**
   * Make a connection to remote JVM.
   */
  private void establishConnection() throws IOException {
    sock = new Socket(vmaddr.inetAddress(), vmaddr.port());
		// throws IOException

    if (MetaVM.tcp_nodelay)
      sock.setTcpNoDelay(true);

    OutputStream o = sock.getOutputStream();
    InputStream i = sock.getInputStream();

    int bufsize = MetaVM.bufsize;
    if (bufsize > 0) {
      bufsize *= 1024;
      o = new BufferedOutputStream(o, bufsize);
      i = new BufferedInputStream(i, bufsize);
    }

    out = new DistObjectOutputStream(o);
    in = new DistObjectInputStream(i);
  }

  private void closeConnection() throws IOException {
    out.writeByte(Protocol.CLOSE);
    out.flush();

    in.readByte();	// CLOSE

    sock.close();
    sock = null;
  }

  protected void finalize() throws Throwable {
    boolean orig = MetaVM.remoteTransparency(false);

    super.finalize();
    closeConnection();

    MetaVM.remoteTransparency(orig);
  }


  /**
   * Examines whether this proxy object is on local JVM.
   */
  public boolean isLocal() {
    boolean orig = MetaVM.remoteTransparency(false);

    boolean ret = this.vmaddr.isLocalAddress();
    if (debug)
      System.out.println("Proxy.isLocal(): " + ret);

    MetaVM.remoteTransparency(orig);

    return ret;
  }

  /**
   * Returns a local object corresponding to this proxy object
   * if this is on local JVM, null if isn't local.
   */
  public Object localObject() {
    boolean orig = MetaVM.remoteTransparency(false);

    Object ret = null;
    if (this.isLocal())
      ret = ObjectID.objectById(this.id);
    else
      ret = null;

    MetaVM.remoteTransparency(orig);

    if (debug)
      System.out.println("Proxy#localObject(): " + ret);

    return ret;
  }


  public int get32field(int slot) throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (debug)
      System.out.println("get32field slot: " + slot);

    if (sock == null)  reference();

    out.writeByte(Protocol.GET32FIELD);
    out.writeInt(slot);
    out.flush();

    int ret = in.readInt();
    if (debug)
      System.out.println("get32field val: " + ret);

    MetaVM.remoteTransparency(orig);

    return ret;
  }

  public long get64field(int slot) throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    out.writeByte(Protocol.GET64FIELD);
    out.writeInt(slot);
    out.flush();

    long ret = in.readLong();
    if (debug)
      System.out.println("get64field: " + ret +
		", 0x" + Long.toHexString(ret) +
		", " + Double.longBitsToDouble(ret));

    MetaVM.remoteTransparency(orig);

    return ret;
  }

  public Object getobjfield(int slot) throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (debug)
      System.out.println("get32field slot: " + slot);

    if (sock == null)  reference();

    out.writeByte(Protocol.GETOBJFIELD);
    out.writeInt(slot);
    out.flush();

    Object ret = null;
    try { ret = in.readDistObject(); }
    catch (ClassNotFoundException e) {
      IOException t = new IOException("class not found: " + e.getMessage());
      MetaVM.remoteTransparency(orig);
      throw t;
    }

    MetaVM.remoteTransparency(orig);

    return ret;
  }


  public void put32field(int slot, int val)
	throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    out.writeByte(Protocol.PUT32FIELD);
    out.writeInt(slot);
    out.writeInt(val);
    out.flush();

    if (in.readByte() != Protocol.OK) {
      MetaVM.remoteTransparency(orig);
      throw new IOException("put32field: couldn't get ack.");
    }

    MetaVM.remoteTransparency(orig);
  }

  public void put64field(int slot, long val)
	throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    out.writeByte(Protocol.PUT64FIELD);
    out.writeInt(slot);
    out.writeLong(val);
    out.flush();

    if (in.readByte() != Protocol.OK) {
      MetaVM.remoteTransparency(orig);
      throw new IOException("put64field: couldn't get ack.");
    }

    MetaVM.remoteTransparency(orig);
  }

  public void putobjfield(int slot, Object val)
	throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    out.writeByte(Protocol.PUTOBJFIELD);
    out.writeInt(slot);
    out.writeDistObject(val);
    out.flush();

    if (in.readByte() != Protocol.OK) {
      MetaVM.remoteTransparency(orig);
      throw new IOException("putobjfield: couldn't get ack.");
    }

    MetaVM.remoteTransparency(orig);
  }


  public int arraylength() throws IOException {
    return this.length;

/*
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    out.writeByte(Protocol.ARYLENGTH);
    out.flush();

    int ret = in.readInt();

    MetaVM.remoteTransparency(orig);

    return ret;
*/
  }


  public int aload32(int index)
	throws ArrayIndexOutOfBoundsException, IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    out.writeByte(Protocol.ARYLOAD32);
    out.writeInt(index);
    out.flush();

    Object exc = null;
    try { exc = in.readDistObject(); }
    catch (ClassNotFoundException e) {
      IOException t = new IOException("class not found: " + e.getMessage());
      MetaVM.remoteTransparency(orig);
      throw t;
    }
    if (exc != null) {	// Exception is occurred
      MetaVM.remoteTransparency(orig);
      throw (ArrayIndexOutOfBoundsException)exc;
    }

    int ret = in.readInt();

    MetaVM.remoteTransparency(orig);

    return ret;
  }

  public long aload64(int index)
	throws ArrayIndexOutOfBoundsException, IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    out.writeByte(Protocol.ARYLOAD64);
    out.writeInt(index);
    out.flush();

    Object exc = null;
    try { exc = in.readDistObject(); }
    catch (ClassNotFoundException e) {
      IOException t = new IOException("class not found: " + e.getMessage());
      MetaVM.remoteTransparency(orig);
      throw t;
    }
    if (exc != null) {	// Exception is occurred
      MetaVM.remoteTransparency(orig);
      throw (ArrayIndexOutOfBoundsException)exc;
    }

    long ret = in.readLong();

    MetaVM.remoteTransparency(orig);

    return ret;
  }

  public Object aloadobj(int index)
	throws ArrayIndexOutOfBoundsException, IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    out.writeByte(Protocol.ARYLOADOBJ);
    out.writeInt(index);
    out.flush();

    Object exc = null;
    try { exc = in.readDistObject(); }
    catch (ClassNotFoundException e) {
      IOException t = new IOException("class not found: " + e.getMessage());
      MetaVM.remoteTransparency(orig);
      throw t;
    }
    if (exc != null) {	// Exception is occurred
      MetaVM.remoteTransparency(orig);
      throw (ArrayIndexOutOfBoundsException)exc;
    }

    Object ret = null;
    try { ret = in.readDistObject(); }
    catch (ClassNotFoundException e) {
      IOException t = new IOException("class not found: " + e.getMessage());
      MetaVM.remoteTransparency(orig);
      throw t;
    }

    MetaVM.remoteTransparency(orig);

    return ret;
  }

  public void astore32(int index, int val)
	throws ArrayIndexOutOfBoundsException, IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    out.writeByte(Protocol.ARYSTORE32);
    out.writeInt(index);
    out.writeInt(val);
    out.flush();

    Object exc = null;
    try { exc = in.readDistObject(); }
    catch (ClassNotFoundException e) {
      IOException t = new IOException("class not found: " + e.getMessage());
      MetaVM.remoteTransparency(orig);
      throw t;
    }
    if (exc != null) {	// Exception is occurred
      MetaVM.remoteTransparency(orig);
      throw (ArrayIndexOutOfBoundsException)exc;
    }

    MetaVM.remoteTransparency(orig);
  }

  public void astore64(int index, long val)
	throws ArrayIndexOutOfBoundsException, IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    out.writeByte(Protocol.ARYSTORE64);
    out.writeInt(index);
    out.writeLong(val);
    out.flush();

    Object exc = null;
    try { exc = in.readDistObject(); }
    catch (ClassNotFoundException e) {
      IOException t = new IOException("class not found: " + e.getMessage());
      MetaVM.remoteTransparency(orig);
      throw t;
    }
    if (exc != null) {	// Exception is occurred
      MetaVM.remoteTransparency(orig);
      throw (ArrayIndexOutOfBoundsException)exc;
    }

    MetaVM.remoteTransparency(orig);
  }

  public void astoreobj(int index, Object val)
	throws ArrayIndexOutOfBoundsException, IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    out.writeByte(Protocol.ARYSTOREOBJ);
    out.writeInt(index);
    out.writeDistObject(val);
    out.flush();

    Object exc = null;
    try { exc = in.readDistObject(); }
    catch (ClassNotFoundException e) {
      IOException t = new IOException("class not found: " + e.getMessage());
      MetaVM.remoteTransparency(orig);
      throw t;
    }
    if (exc != null) {	// Exception is occurred
      MetaVM.remoteTransparency(orig);
      throw (ArrayIndexOutOfBoundsException)exc;
    }

    MetaVM.remoteTransparency(orig);
  }


  public Object invoke(int slot, int mbindex, Object[] args)
	throws Throwable {
    boolean orig = MetaVM.remoteTransparency(false);

    if (debug) {
      System.out.println("Proxy.invoke() called.");
      System.out.println(" clazz: " + this.clz.getName());
      System.out.println(" slot: " + slot);
      System.out.println(" mbindex: " + mbindex);
      System.out.println(" args: " + args);
      System.out.flush();

      if (args != null) {
	for (int i = 0; i < args.length; i++) {
	  System.out.println("  args[" + i + "]: " +
			args[i] + " " + args[i].getClass());
	}
	System.out.flush();
      }

      if (args != null) {
	//Class clazz = Object[].class;
	Class clazz = args.getClass();
	System.out.println(clazz);

	java.io.ObjectStreamClass oclz =
			java.io.ObjectStreamClass.lookup(clazz);
	System.out.println(oclz);
      }
    }

/*
    if (VMOperations.isNativeMethod(this.clz, slot, mbindex) &&
	args != null) {
      // check if there are args which is Proxy class
      for (int i = 0; i < args.length; i++) {
	boolean remoteRef = false;
	if (args[i] instanceof Proxy) {	// Proxy
	  Proxy anArg = (Proxy)args[i];
	  if (!(anArg.address().equals(vmaddr)))  remoteRef = true;
	}
	else {				// not Proxy
	  if (!vmaddr.isLocalAddress())  remoteRef = true;
	}
	if (remoteRef)
	  System.err.println(
	      "[MetaVM: (at Proxy) Remote refs passed to the native method: " +
	      this.clz.getName() + "#" +
	      VMOperations.MethodName(this.clz, slot, mbindex) +
	      ", slot: " + slot + ", arg#: " + i + "]");
      }
    }
*/

    if (sock == null)  reference();

    out.writeByte(Protocol.INVOKE);
    out.writeShort(slot);
    if (slot == 0)  out.writeShort(mbindex);
    out.writeDistObject(new ByValueWrapper(args));
    out.flush();

    if (in.readByte() != Protocol.OK) {
      if (debug) {
	System.out.println("Proxy.invoke(): an exception thrown:");
	System.out.flush();
      }

      Object thrw = null;
      try { thrw = in.readDistObject(); }
      catch (ClassNotFoundException e) {
	IOException t = new IOException("class not found: " + e.getMessage());
	MetaVM.remoteTransparency(orig);
	throw t;
      }
      if (thrw != null) {	// Throwable is thrown at remote machine
	if (debug) {
	  System.out.println("  throwing: " + thrw);
	  System.out.flush();
	}

	MetaVM.remoteTransparency(orig);
	throw (Throwable)thrw;
      }
    }

    Object ret = null;
    try { ret = in.readDistObject(); }
    catch (ClassNotFoundException e) {
      IOException t = new IOException("class not found " + e.getMessage());
      MetaVM.remoteTransparency(orig);
      throw t;
    }

    MetaVM.remoteTransparency(orig);		// restore original remote flag

    if (debug) {
      System.out.println("Proxy.invoke() done.");
      System.out.flush();
    }
    return ret;
  }


  public void monitorEnter() throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    out.writeByte(Protocol.MONITORENTER);
    out.flush();

    if (in.readByte() != Protocol.OK) {
      MetaVM.remoteTransparency(orig);
      throw new IOException("couldn't get a monitor.");
    }

    MetaVM.remoteTransparency(orig);
  }


  public void monitorExit() throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    out.writeByte(Protocol.MONITOREXIT);
    out.flush();

    if (in.readByte() != Protocol.OK) {
      MetaVM.remoteTransparency(orig);
      throw new IOException("couldn't release a monitor.");
    }

    MetaVM.remoteTransparency(orig);
  }


  public Object getObjCopy() throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    out.writeByte(Protocol.GETOBJCOPY);
    out.flush();

    ByValueWrapper wrap = null;
    Object ret = null;
    if (in.readByte() == Protocol.OK) {
      try { wrap = (ByValueWrapper)in.readDistObject(); }
      catch (ClassNotFoundException e) {
	IOException t = new IOException("class not found " + e.getMessage());
	MetaVM.remoteTransparency(orig);
	throw t;
      }

      ret = wrap.target();
    }

    MetaVM.remoteTransparency(orig);

    return ret;
  }


  public Object getArrayCopy(int pos, int len) throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    // range check
    if ((pos < 0) || (pos >= this.length)) {
      MetaVM.remoteTransparency(orig);
      throw new ArrayIndexOutOfBoundsException("invalid position: " + pos);
    }
    if (len > this.length - pos) {
		// allow negative len, which means infinite
      MetaVM.remoteTransparency(orig);
      throw new ArrayIndexOutOfBoundsException("invalid length: " + len);
    }

    out.writeByte(Protocol.GETARYCOPY);
    out.writeInt(pos);
    out.writeInt(len);
    out.flush();

    ByValueWrapper wrap = null;
    Object ret = null;
    if (in.readByte() == Protocol.OK) {
      try { wrap = (ByValueWrapper)in.readDistObject(); }
      catch (ClassNotFoundException e) {
	IOException t = new IOException("class not found " + e.getMessage());
	MetaVM.remoteTransparency(orig);
	throw t;
      }

      ret = wrap.target();
    }

    MetaVM.remoteTransparency(orig);

    return ret;
  }


  public void aryBlockLoad(Object dst, int srcpos, int len, int dstpos)
	throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    // range check
    if ((srcpos < 0) || (srcpos >= this.length)) {
      MetaVM.remoteTransparency(orig);
      throw new ArrayIndexOutOfBoundsException(
			"invalid position in source: " + srcpos);
    }
    if (dstpos < 0 /* || (dstpos >= ((...[])dst).length) */) {
      MetaVM.remoteTransparency(orig);
      throw new ArrayIndexOutOfBoundsException(
			"invalid position in destination: " + dstpos);
    }
    if ((len < 0) || (len > this.length - srcpos)) {
      MetaVM.remoteTransparency(orig);
      throw new ArrayIndexOutOfBoundsException("invalid length: " + len);
    }

    // type check (incomplete)
    String cname = dst.getClass().getName();
    if (!cname.startsWith("[")) {
      MetaVM.remoteTransparency(orig);
      throw new IOException("destination object is not an array.");
    }

    char componentTypecode = cname.charAt(1);

    out.writeByte(Protocol.ARYBLOCKLOAD);
    out.writeChar(componentTypecode);
    out.writeInt(srcpos);
    out.writeInt(len);
    out.flush();

    if (in.readByte() != Protocol.OK) {
      // not reached
      MetaVM.remoteTransparency(orig);
      throw new IOException("error occurred.");
    }

    int i;
    switch (componentTypecode) {
    case 'L':
      Object[] larray = (Object[])dst;
      try {
	for (i = 0; i < len; i++)  larray[dstpos + i] = in.readDistObject();
      }
      catch (ClassNotFoundException e) {
	IOException t = new IOException("class not found " + e.getMessage());
	MetaVM.remoteTransparency(orig);
	throw t;
      }
      break;
    case 'Z':
      boolean[] zarray = (boolean[])dst;
      for (i = 0; i < len; i++)  zarray[dstpos + i] = in.readBoolean();
      break;
    case 'B':
      byte[] barray = (byte[])dst;
      for (i = 0; i < len; i++)  barray[dstpos + i] = in.readByte();
      break;
    case 'C':
      char[] carray = (char[])dst;
      for (i = 0; i < len; i++)  carray[dstpos + i] = in.readChar();
      break;
    case 'S':
      short[] sarray = (short[])dst;
      for (i = 0; i < len; i++)  sarray[dstpos + i] = in.readShort();
      break;
    case 'I':
      int[] iarray = (int[])dst;
      for (i = 0; i < len; i++)  iarray[dstpos + i] = in.readInt();
      break;
    case 'J':
      long[] jarray = (long[])dst;
      for (i = 0; i < len; i++)  jarray[dstpos + i] = in.readLong();
      break;
    case 'F':
      float[] farray = (float[])dst;
      for (i = 0; i < len; i++)  farray[dstpos + i] = in.readFloat();
      break;
    case 'D':
      double[] darray = (double[])dst;
      for (i = 0; i < len; i++)  darray[dstpos + i] = in.readDouble();
      break;
    }

    MetaVM.remoteTransparency(orig);
  }


  public void aryBlockStore(Object src, int srcpos, int len, int dstpos)
	throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);

    if (sock == null)  reference();

    // range check
    if ((dstpos < 0) || (dstpos >= this.length)) {
      MetaVM.remoteTransparency(orig);
      throw new ArrayIndexOutOfBoundsException(
			"invalid position in destination: " + dstpos);
    }
    if (srcpos < 0 /* || (srcpos >= ((...[])dst).length) */) {
      MetaVM.remoteTransparency(orig);
      throw new ArrayIndexOutOfBoundsException(
			"invalid position in source: " + srcpos);
    }
    if ((len < 0) /* || (len > ((...[])src).length) */) {
      MetaVM.remoteTransparency(orig);
      throw new ArrayIndexOutOfBoundsException("invalid length: " + len);
    }

    // type check (incomplete)
    String cname = src.getClass().getName();
    if (!cname.startsWith("[")) {
      MetaVM.remoteTransparency(orig);
      throw new IOException("source object is not an array.");
    }

    char componentTypecode = cname.charAt(1);

    out.writeByte(Protocol.ARYBLOCKSTORE);
    out.writeChar(componentTypecode);
    out.writeInt(dstpos);
    out.writeInt(len);
    out.flush();

    if (in.readByte() != Protocol.OK) {
      // not reached
      MetaVM.remoteTransparency(orig);
      throw new IOException("error occurred.");
    }

    int i;
    switch (componentTypecode) {
    case 'L':
      Object[] larray = (Object[])src;
      for (i = 0; i < len; i++)  out.writeDistObject(larray[srcpos + i]);
      break;
    case 'Z':
      boolean[] zarray = (boolean[])src;
      for (i = 0; i < len; i++)  out.writeBoolean(zarray[srcpos + i]);
      break;
    case 'B':
      byte[] barray = (byte[])src;
      for (i = 0; i < len; i++)  out.writeByte(barray[srcpos + i]);
      break;
    case 'C':
      char[] carray = (char[])src;
      for (i = 0; i < len; i++)  out.writeChar(carray[srcpos + i]);
      break;
    case 'S':
      short[] sarray = (short[])src;
      for (i = 0; i < len; i++)  out.writeShort(sarray[srcpos + i]);
      break;
    case 'I':
      int[] iarray = (int[])src;
      for (i = 0; i < len; i++)  out.writeInt(iarray[srcpos + i]);
      break;
    case 'J':
      long[] jarray = (long[])src;
      for (i = 0; i < len; i++)  out.writeLong(jarray[srcpos + i]);
      break;
    case 'F':
      float[] farray = (float[])src;
      for (i = 0; i < len; i++)  out.writeFloat(farray[srcpos + i]);
      break;
    case 'D':
      double[] darray = (double[])src;
      for (i = 0; i < len; i++)  out.writeDouble(darray[srcpos + i]);
      break;
    }

    MetaVM.remoteTransparency(orig);
  }


  private void writeObject(java.io.ObjectOutputStream out)
	throws java.io.IOException {
    out.writeObject(this.vmaddr);
    out.writeObject(this.clzsrc);
    out.writeObject(this.clz);
    out.writeInt(this.id);
    out.writeInt(this.length);
  }

  private void readObject(java.io.ObjectInputStream in)
	throws java.io.IOException, ClassNotFoundException {
    this.vmaddr = (VMAddress)in.readObject();
    this.clzsrc = (VMAddress)in.readObject();

    if (in instanceof DistObjectInputStream) {
      DistObjectInputStream din = (DistObjectInputStream)in;

      ClassLoader cl = null;
      if (MetaVM.load_class_locally || this.clzsrc.isLocalAddress())
	cl = MetaVM.systemClassLoader;
      else
	cl = RemoteClassLoader.get(this.clzsrc);

      ClassLoader orig = din.classLoader(cl);	// set and save the original
      this.clz = (Class)din.readDistObject();
      din.classLoader(orig);			// restore the original
    }
    else
      this.clz = (Class)in.readObject();

    this.id = in.readInt();
    this.length = in.readInt();
  }
}
