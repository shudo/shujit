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

import java.net.Socket;
import java.io.IOException;
import java.io.OutputStream;
import java.io.InputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedInputStream;
import java.lang.reflect.Array;


/**
 * Request server serves a connection from proxy object.
 */
public class Skeleton implements Runnable {
  private boolean debug = false;

  private MetaVMServer server;
  private Socket sock;
  private DistObjectOutputStream out;
  private DistObjectInputStream in;

  private Object obj = null;
  private int length = -1;	// length if obj is array

  private Class clz = null;
  private String signature = null;
  private char elemsig;
  private void setSignature() {
    if (obj != null) {
      this.clz = obj.getClass();
      this.signature = clz.getName();
      if (debug)  System.out.println("sig: " + this.signature);
      try {
	if (this.signature.charAt(0) == '[')
	  this.elemsig = this.signature.charAt(1);
      }
      catch (StringIndexOutOfBoundsException e) {
	if (debug)  e.printStackTrace();
      }
    }
  }


  protected Skeleton(MetaVMServer serv, Socket sock) throws IOException {
    this.server = serv;
    init(sock);
  }
  protected Skeleton(Socket sock) throws IOException {
    this(null, sock);
  }

  protected void init(Socket sock) throws IOException {
    this.debug = MetaVM.debug;

    this.sock = sock;
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


  private void close() {
    if (sock != null) {
      try { sock.close(); }
      catch (IOException e) {}
      sock = null;
    }
  }


  /**
   * An utility method for REFERENCE message.
   */
  private void reference(Object obj) throws IOException {
    this.obj = obj;

    if (obj != null) {
      out.writeByte(Protocol.OK);

      Class clazz = obj.getClass();

      // set Skeleton#length
      if (clazz.isArray()) {
	this.length = Array.getLength(obj);
      }

      VMAddress classsrc = null;
      ClassLoader cl = clazz.getClassLoader();
      if ((cl != null) && (cl instanceof RemoteClassLoader))
	classsrc = ((RemoteClassLoader)cl).sourceAddress();
      else
	classsrc = VMAddress.localAddress();
      if (MetaVM.debug) {
	System.out.println("classsrc is " + classsrc + ".");
	System.out.flush();
      }
      out.writeObject(classsrc);

      String classname = clazz.getName();
      if (debug) {
	boolean orig = MetaVM.remoteTransparency(false);
	System.out.println("obj: " + obj);
	System.out.println("class: " + classname);
	MetaVM.remoteTransparency(orig);
      }
      out.writeUTF(classname);

      out.writeInt(ObjectID.idByObject(obj));

      if (obj.getClass().isArray())
	length = Array.getLength(obj);
      else
	length = 0;
      out.writeInt(length);
    }
    else {
      out.writeByte(Protocol.ERROR);
    }

    out.flush();

    setSignature();
  }


  public void run() {
    boolean done = false;
    int req;

    int id, slot, length;

    try {
//System.out.println("ReqServer#run() called.");
//System.out.flush();
      while (!done) {
	req = in.readByte();
	if (debug) {
	  boolean orig = MetaVM.remoteTransparency(false);
	  System.out.println("request: " + req);
	  MetaVM.remoteTransparency(orig);
	}

	switch (req) {
	case Protocol.CLOSE:
	  if (debug)
	    System.out.println("CLOSE");
	  {
	    out.writeByte(Protocol.CLOSE);
	    out.flush();

	    close();
	    done = true;
	  }
	  break;

	case Protocol.REFERENCE:
	  if (debug)
	    System.out.println("REFERENCE");
	  {
	    id = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("id: " + id);
	      MetaVM.remoteTransparency(orig);
	    }

	    this.obj = ExportTable.get(id);

	    reference(obj);
	  }
	  break;

	case Protocol.REFREGISTRY:
	  if (debug)
	    System.out.println("REFREGISTRY");
	  {
	    reference(this.server.registry);
	  }
	  break;

	case Protocol.NEW:
	  if (debug)
	    System.out.println("NEW");
	  {
	    VMAddress classsrc = null;
	    String classname;
	    Class clazz = null;

	    try {
	      classsrc = (VMAddress)in.readObject();
	    }
	    catch (ClassNotFoundException e) {
	      // not reached
	      boolean orig = MetaVM.remoteTransparency(false);
	      if (debug)  e.printStackTrace();
	      MetaVM.remoteTransparency(orig);
	    }

	    classname = in.readUTF();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("class source: " + classsrc);
	      System.out.println("classname: " + classname);
	      MetaVM.remoteTransparency(orig);
	    }

	    try {
	      ClassLoader cl;
	      if (MetaVM.load_class_locally || classsrc.isLocalAddress())
		cl = MetaVM.systemClassLoader;
	      else
		cl = RemoteClassLoader.get(classsrc);
	      in.classLoader(cl);	// set class loader

	      clazz = cl.loadClass(classname);

	      this.obj = VMOperations.instantiate(clazz);
	      id = ObjectID.idByObject(this.obj);
	    }
	    catch (ClassNotFoundException e) {
	      id = 0;
	    }
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("id: " + id +
			" (0x" + Integer.toHexString(id) + ")");
	      MetaVM.remoteTransparency(orig);
	    }

	    out.writeInt(id);
	    out.flush();

	    setSignature();
	    ExportTable.register(this.obj);
	  }
	  break;

	case Protocol.NEWARRAY:
	  if (debug)
	    System.out.println("NEWARRAY");
	  {
	    VMAddress classsrc = null;
	    byte type;
	    int count;

	    try {
	      ClassLoader cl;
	      classsrc = (VMAddress)in.readObject();

	      if (MetaVM.load_class_locally || classsrc.isLocalAddress())
		cl = MetaVM.systemClassLoader;
	      else
		cl = RemoteClassLoader.get(classsrc);
	      in.classLoader(cl);	// set class loader
	    }
	    catch (ClassNotFoundException e) {
	      // not reached
	      boolean orig = MetaVM.remoteTransparency(false);
	      if (debug)  e.printStackTrace();
	      MetaVM.remoteTransparency(orig);
	    }

	    type = in.readByte();
	    count = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("class source: " + classsrc);
	      System.out.println("type, count: " + type + ", " + count);
	      MetaVM.remoteTransparency(orig);
	    }

	    this.obj = VMOperations.newarray(type, count);
	    this.length = count;
	    id = ObjectID.idByObject(this.obj);

	    out.writeInt(id);
	    out.flush();

	    setSignature();
	    ExportTable.register(this.obj);
	  }
	  break;

	case Protocol.ANEWARRAY:
	  if (debug)
	    System.out.println("ANEWARRAY");
	  {
	    VMAddress classsrc = null;
	    ClassLoader cl = null;
	    String classname;
	    int count;
	    Class clazz = null;

	    try {
	      classsrc = (VMAddress)in.readObject();
	      if (MetaVM.load_class_locally || classsrc.isLocalAddress())
		cl = MetaVM.systemClassLoader;
	      else
		cl = RemoteClassLoader.get(classsrc);
	      in.classLoader(cl);	// set class loader
	    }
	    catch (ClassNotFoundException e) {
	      // not reached
	      boolean orig = MetaVM.remoteTransparency(false);
	      if (debug)  e.printStackTrace();
	      MetaVM.remoteTransparency(orig);
	    }

	    classname = in.readUTF();
	    count = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("class source: " + classsrc);
	      System.out.println("classname: " + classname);
	      System.out.println("count: " + count);
	      MetaVM.remoteTransparency(orig);
	    }

	    try {
	      if (cl == null)
		clazz = Class.forName(classname);
	      else
		clazz = cl.loadClass(classname);
	      this.obj = VMOperations.anewarray(clazz, count);
	      this.length = count;
	      id = ObjectID.idByObject(this.obj);
	    }
	    catch (ClassNotFoundException e) {
	      id = 0;
	    }

	    out.writeInt(id);
	    out.flush();

	    setSignature();
	    ExportTable.register(this.obj);
	  }
	  break;

	case Protocol.MULTIANEWARRAY:
	  if (debug)
	    System.out.println("MULTIANEWARRAY");
	  {
	    VMAddress classsrc = null;
	    ClassLoader cl = null;
	    String classname;
	    int[] sizes = null;
	    Class clazz = null;

	    try {
	      classsrc = (VMAddress)in.readObject();

	      if (MetaVM.load_class_locally || classsrc.isLocalAddress())
		cl = MetaVM.systemClassLoader;
	      else
		cl = RemoteClassLoader.get(classsrc);
	      in.classLoader(cl);	// set class loader
	    }
	    catch (ClassNotFoundException e) {
	      // not reached
	      boolean orig = MetaVM.remoteTransparency(false);
	      if (debug)  e.printStackTrace();
	      MetaVM.remoteTransparency(orig);
	    }

	    classname = in.readUTF();
	    try {
	      ByValueWrapper sizes_wrapper =
			(ByValueWrapper)in.readDistObject();
	      sizes = (int[])sizes_wrapper.target();
	    }
	    catch (ClassNotFoundException e) {
	      // not reached
	      boolean orig = MetaVM.remoteTransparency(false);
	      if (debug)  e.printStackTrace();
	      MetaVM.remoteTransparency(orig);
	    }
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("classname: " + classname);
	      System.out.println("dimension: " + sizes.length);
	      for (int i = 0; i < sizes.length; i++)
		System.out.println("sizes[" + i + "]: " + sizes[i]);
	      MetaVM.remoteTransparency(orig);
	    }

	    try {
	      if (cl == null)
		clazz = Class.forName(classname);
	      else
		clazz = cl.loadClass(classname);
	      this.obj = VMOperations.multianewarray(clazz, sizes);
	      this.length = sizes[0];
	      id = ObjectID.idByObject(this.obj);
	    }
	    catch (ClassNotFoundException e) {
	      id = 0;
	    }

	    out.writeInt(id);
	    out.flush();

	    setSignature();
	    ExportTable.register(this.obj);
	  }
	  break;

	case Protocol.GET32FIELD:
	  if (debug)
	    System.out.println("GET32FIELD");
	  {
	    slot = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("slot: " + slot);
	      MetaVM.remoteTransparency(orig);
	    }

	    int val = 0;
	    val = VMOperations.get32Field(this.obj, slot);
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("val: " + val);
	      MetaVM.remoteTransparency(orig);
	    }

	    out.writeInt(val);
	    out.flush();
	  }
	  break;

	case Protocol.GET64FIELD:
	  if (debug)
	    System.out.println("GET64FIELD");
	  {
	    slot = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("slot: " + slot);
	      MetaVM.remoteTransparency(orig);
	    }

	    long val = 0;
	    val = VMOperations.get64Field(this.obj, slot);
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("val: " + val +
			", " + Double.longBitsToDouble(val));
	      MetaVM.remoteTransparency(orig);
	    }

	    out.writeLong(val);
	    out.flush();
	  }
	  break;

	case Protocol.GETOBJFIELD:
	  if (debug)
	    System.out.println("GETOBJFIELD");
	  {
	    slot = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("slot: " + slot);
	      MetaVM.remoteTransparency(orig);
	    }

	    Object val = null;
	    val = VMOperations.getObjectField(this.obj, slot);

	    out.writeDistObject(val);
	    out.flush();
	  }
	  break;

	case Protocol.PUT32FIELD:
	  if (debug)
	    System.out.println("PUT32FIELD");
	  {
	    int val;

	    slot = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("slot: " + slot);
	      MetaVM.remoteTransparency(orig);
	    }

	    val = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("val: " + val);
	      MetaVM.remoteTransparency(orig);
	    }
	    out.writeByte(Protocol.OK);
	    out.flush();

	    VMOperations.put32Field(this.obj, slot, val);
	  }
	  break;

	case Protocol.PUT64FIELD:
	  if (debug)
	    System.out.println("PUT64FIELD");
	  {
	    long val;

	    slot = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("slot: " + slot);
	      MetaVM.remoteTransparency(orig);
	    }

	    val = in.readLong();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("val: " + val +
			", " + Double.longBitsToDouble(val));
	      MetaVM.remoteTransparency(orig);
	    }
	    out.writeByte(Protocol.OK);
	    out.flush();

	    VMOperations.put64Field(this.obj, slot, val);
	  }
	  break;

	case Protocol.PUTOBJFIELD:
	  if (debug)
	    System.out.println("PUTOBJFIELD");
	  {
	    Object val;

	    slot = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("slot: " + slot);
	      MetaVM.remoteTransparency(orig);
	    }

	    try {
	      val = in.readDistObject();
	      if (debug) {
		boolean orig = MetaVM.remoteTransparency(false);
		System.out.println(val);
		MetaVM.remoteTransparency(orig);
	      }

	      out.writeByte(Protocol.OK);
	      out.flush();

	      VMOperations.putObjectField(this.obj, slot, val);
	    }
	    catch (ClassNotFoundException ce) {
	      out.writeByte(Protocol.ERROR);
	      out.flush();
	    }
	  }
	  break;

	case Protocol.ARYLENGTH:
	  if (debug)
	    System.out.println("ARYLENGTH");
	  {
	    int result = -1;

	    {
	      switch (elemsig) {
	      case 'Z':
		result = ((boolean[])this.obj).length;  break;
	      case 'B':
		result = ((byte[])this.obj).length;  break;
	      case 'S':
		result = ((short[])this.obj).length;  break;
	      case 'C':
		result = ((char[])this.obj).length;  break;
	      case 'I':
		result = ((int[])this.obj).length;  break;
	      case 'F':
		result = ((float[])this.obj).length;  break;
	      case 'J':
		result = ((long[])this.obj).length;  break;
	      case 'D':
		result = ((double[])this.obj).length;  break;
	      default:
		if (debug) {
		  boolean orig = MetaVM.remoteTransparency(false);
		  System.err.println("invalid element signature: " + elemsig);
		  MetaVM.remoteTransparency(orig);
		}
		break;
	      }	// switch (elemsig)
	    }

	    out.writeInt(result);
	  }
	  break;

	case Protocol.ARYLOAD32:
	  if (debug)
	    System.out.println("ARYLOAD32");
	  {
	    int index;
	    int result;

	    index = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("index: " + index);
	      MetaVM.remoteTransparency(orig);
	    }

	    try {
	      switch (elemsig) {
	      case 'Z': {
		  boolean val = ((boolean[])this.obj)[index];
		  if (val)  result = 1;
		  else  result = 0;
		} break;
	      case 'B': {
		  byte val = ((byte[])this.obj)[index];
		  result = (int)val;
		} break;
	      case 'S': {
		  short val = ((short[])this.obj)[index];
		  result = (int)val;
		} break;
	      case 'C': {
		  char val = ((char[])this.obj)[index];
		  result = (int)val;
		} break;
	      case 'I':
		result = ((int[])this.obj)[index];
		break;
	      case 'F': {
		  float val = ((float[])this.obj)[index];
		  result = Float.floatToIntBits(val);
		} break;
	      default:
		result = 0;
		if (debug) {
		  boolean orig = MetaVM.remoteTransparency(false);
		  System.err.println("invalid element signature: " + elemsig);
		  MetaVM.remoteTransparency(orig);
		}
		break;
	      }	// switch (elemsig)

	      out.writeDistObject(null);
	      out.writeInt(result);
	    }
	    catch (ArrayIndexOutOfBoundsException e) {
	      out.writeDistObject(e);
	    }

	    out.flush();
	  }
	  break;

	case Protocol.ARYLOAD64:
	  if (debug)
	    System.out.println("ARYLOAD64");
	  {
	    int index;
	    long result;

	    index = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("index: " + index);
	      MetaVM.remoteTransparency(orig);
	    }

	    try {
	      switch (elemsig) {
	      case 'J':
		result = ((long[])this.obj)[index];
		break;
	      case 'D':
		{
		  double val = ((double[])this.obj)[index];
		  result = Double.doubleToLongBits(val);
		}
		break;
	      default:
		result = 0;
		if (debug) {
		  boolean orig = MetaVM.remoteTransparency(false);
		  System.err.println("invalid element signature: " + elemsig);
		  MetaVM.remoteTransparency(orig);
		}
		break;
	      }

	      out.writeDistObject(null);
	      out.writeLong(result);
	    }
	    catch (ArrayIndexOutOfBoundsException e) {
	      out.writeDistObject(e);
	    }

	    out.flush();
	  }
	  break;

	case Protocol.ARYLOADOBJ:
	  if (debug)
	    System.out.println("ARYLOADOBJ");
	  {
	    int index;
	    Object result;

	    index = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("index: " + index);
	      MetaVM.remoteTransparency(orig);
	    }
	    
	    try {
	      result = ((Object[])this.obj)[index];

	      out.writeDistObject(null);
	      out.writeDistObject(result);
	    }
	    catch (ArrayIndexOutOfBoundsException e) {
	      out.writeDistObject(e);
	    }

	    out.flush();
	  }
	  break;

	case Protocol.ARYSTORE32:
	  if (debug)
	    System.out.println("ARYSTORE32");
	  {
	    int index;
	    int val;

	    index = in.readInt();
	    val = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("index, val: " + index + ", " + val
			+ ", " + Float.intBitsToFloat(val));
	      MetaVM.remoteTransparency(orig);
	    }

	    try {
	      switch (elemsig) {
	      case 'Z':
		((boolean[])this.obj)[index] = ((val != 0) ? true : false);
		break;
	      case 'B':
		((byte[])this.obj)[index] = (byte)val;
		break;
	      case 'S':
		((short[])this.obj)[index] = (short)val;
		break;
	      case 'C':
		((char[])this.obj)[index] = (char)val;
		break;
	      case 'I':
		((int[])this.obj)[index] = val;
		break;
	      case 'F':
		((float[])this.obj)[index] = Float.intBitsToFloat(val);
		break;
	      default:
		if (debug) {
		  boolean orig = MetaVM.remoteTransparency(false);
		  System.err.println("invalid element signature: " + elemsig);
		  MetaVM.remoteTransparency(orig);
		}
		break;
	      }

	      out.writeDistObject(null);
	    }
	    catch (ArrayIndexOutOfBoundsException e) {
	      out.writeDistObject(e);
	    }

	    out.flush();
	  }
	  break;

	case Protocol.ARYSTORE64:
	  if (debug)
	    System.out.println("ARYSTORE64");
	  {
	    int index;
	    long val;

	    index = in.readInt();
	    val = in.readLong();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("index, val: " + index + ", " + val
			+ ", " + Double.longBitsToDouble(val));
	      MetaVM.remoteTransparency(orig);
	    }

	    try {
	      switch (elemsig) {
	      case 'J':
		((long[])this.obj)[index] = val;
		break;
	      case 'D':
		((double[])this.obj)[index] = Double.longBitsToDouble(val);
		break;
	      default:
		if (debug) {
		  boolean orig = MetaVM.remoteTransparency(false);
		  System.err.println("invalid element signature: " + elemsig);
		  MetaVM.remoteTransparency(orig);
		}
		break;
	      }

	      out.writeDistObject(null);
	    }
	    catch (ArrayIndexOutOfBoundsException e) {
	      out.writeDistObject(e);
	    }

	    out.flush();
	  }
	  break;

	case Protocol.ARYSTOREOBJ:
	  if (debug)
	    System.out.println("ARYSTOREOBJ");
	  {
	    int index;
	    Object val;

	    index = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("index: " + index);
	      MetaVM.remoteTransparency(orig);
	    }

	    try { val = in.readDistObject(); }
	    catch (ClassNotFoundException e) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      e.printStackTrace();
	      MetaVM.remoteTransparency(orig);

	      throw new IOException(e.getMessage());
	    }

	    try {
	      ((Object[])this.obj)[index] = val;

	      out.writeDistObject(null);
	    }
	    catch (ArrayIndexOutOfBoundsException e) {
	      out.writeDistObject(e);
	    }

	    out.flush();
	  }
	  break;

	case Protocol.INVOKE:
	  if (debug)
	    System.out.println("INVOKE");
	  {
	    int mbindex = 0;
	    Object[] args = null;
	    Object result;
	    boolean proxyArgs = false;

	    // receive slot no. of the callee
	    slot = (int)in.readUnsignedShort();
	    if (slot == 0)  mbindex = (int)in.readUnsignedShort();

	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("slot: " + slot);
	      MetaVM.remoteTransparency(orig);
	    }

	    // receive arguments
	    try {
	      ByValueWrapper args_wrapper =
			(ByValueWrapper)in.readDistObject();
	      args = (Object[])args_wrapper.target();
	    }
	    catch (ClassNotFoundException e) {
	      // not reached.
	      boolean orig = MetaVM.remoteTransparency(false);
	      if (debug)  e.printStackTrace();
	      MetaVM.remoteTransparency(orig);
	    }

/*
	    if (VMOperations.isNativeMethod(this.clz, slot, mbindex) &&
		args != null) {
	      // check if there are args which is Proxy class
	      boolean orig = MetaVM.remoteTransparency(false);
	      for (int i = 0; i < args.length; i++) {
		if (args[i] instanceof Proxy) {
		  System.err.println(
		    "[MetaVM: (at Skeleton) Remote refs passed to the native method: " +
		    this.clz.getName() + "#" +
		    VMOperations.MethodName(this.clz, slot, mbindex) +
		    ", slot: " + slot + ", arg#: " + i + "]");
		}
	      }
	      MetaVM.remoteTransparency(orig);
	    }
*/

	    try {
	      result = VMOperations.invoke(this.obj, slot, mbindex, args);

	      out.writeByte(Protocol.OK);
	      out.writeDistObject(result);
	    }
	    catch (Throwable t) {
	      boolean orig = MetaVM.remoteTransparency(false);

	      if (debug) {
		System.out.println("Skeleton INVOKE: an exception thrown:");
		t.printStackTrace();
	      }

	      out.writeByte(Protocol.ERROR);
	      out.writeDistObject(t);

	      MetaVM.remoteTransparency(orig);
	    }

	    out.flush();
	  }
	  break;

	case Protocol.MONITORENTER:
	  if (debug)
	    System.out.println("MONITORENTER");
	  {
	    VMOperations.monitorEnter(this.obj);
	    out.writeByte(Protocol.OK);
	    out.flush();
	  }
	  break;

	case Protocol.MONITOREXIT:
	  if (debug)
	    System.out.println("MONITOREXIT");
	  {
	    VMOperations.monitorExit(this.obj);
	    out.writeByte(Protocol.OK);
	    out.flush();
	  }
	  break;

	case Protocol.GETOBJCOPY:
	  if (debug)
	    System.out.println("GETOBJCOPY");
	  {
	    out.writeByte(Protocol.OK);
	    out.reset();	// call ObjectOutputStream#reset()
	    out.writeDistObject(new ByValueWrapper(this.obj));
	    out.flush();
	  }
	  break;

	case Protocol.GETARYCOPY:
	  if (debug)
	    System.out.println("GETARYCOPY");
	  {
	    int position, len;

	    position = in.readInt();
	    len = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("position: " + position);
	      System.out.println("length: " + len);
	      MetaVM.remoteTransparency(orig);
	    }

	    if (!obj.getClass().isArray() /* not array */ || 
		(position < 0) || (position >= this.length)) {
	      out.writeByte(Protocol.ERROR);
	      out.flush();
	    }
	    else {				// array
	      out.writeByte(Protocol.OK);

	      // normalize length
	      if (len < 0)  len = Integer.MAX_VALUE;
	      len = Math.min(this.length - position, len);

	      // make a partial array to be sent
	      Object partialArray =
		Array.newInstance(TypeUtil.componentType(obj.getClass()), len);
	      System.arraycopy(this.obj, position, partialArray, 0, len);

	      out.writeDistObject(new ByValueWrapper(partialArray));
	      out.flush();
	    }
	  }
	  break;

	case Protocol.ARYBLOCKLOAD:
	  if (debug)
	    System.out.println("ARYBLOCKLOAD");
	  {
	    char componentTypecode;
	    int srcpos, len;

	    componentTypecode = in.readChar();
	    srcpos = in.readInt();
	    len = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("componentType: " + componentTypecode);
	      System.out.println("position: " + srcpos);
	      System.out.println("length  : " + len);
	      MetaVM.remoteTransparency(orig);
	    }

	    if (!obj.getClass().isArray() /* not array */ ||
		(srcpos < 0) || (srcpos >= this.length)) {
	      out.writeByte(Protocol.ERROR);
	      out.flush();
	    }
	    else {
	      out.writeByte(Protocol.OK);
	      out.flush();

	      // normalize length
	      if (len < 0)  len = Integer.MAX_VALUE;
	      len = Math.min(this.length - srcpos, len);

	      // send elements
	      int i;
	      switch (componentTypecode) {
	      case 'L':
		Object[] larray = (Object[])this.obj;
		for (i = 0; i < len; i++)
		  out.writeDistObject(larray[srcpos + i]);
		break;
	      case 'Z':
		boolean[] zarray = (boolean[])this.obj;
		for (i = 0; i < len; i++)
		  out.writeBoolean(zarray[srcpos + i]);
		break;
	      case 'B':
		byte[] barray = (byte[])this.obj;
		for (i = 0; i < len; i++)
		  out.writeByte(barray[srcpos + i]);
		break;
	      case 'C':
		char[] carray = (char[])this.obj;
		for (i = 0; i < len; i++)
		  out.writeChar(carray[srcpos + i]);
		break;
	      case 'S':
		short[] sarray = (short[])this.obj;
		for (i = 0; i < len; i++)
		  out.writeShort(sarray[srcpos + i]);
		break;
	      case 'I':
		int[] iarray = (int[])this.obj;
		for (i = 0; i < len; i++)
		  out.writeInt(iarray[srcpos + i]);
		break;
	      case 'J':
		long[] jarray = (long[])this.obj;
		for (i = 0; i < len; i++)
		  out.writeLong(jarray[srcpos + i]);
		break;
	      case 'F':
		float[] farray = (float[])this.obj;
		for (i = 0; i < len; i++)
		  out.writeFloat(farray[srcpos + i]);
		break;
	      case 'D':
		double[] darray = (double[])this.obj;
		for (i = 0; i < len; i++)
		  out.writeDouble(darray[srcpos + i]);
		break;
	      }

	      out.flush();
	    }
	  }
	  break;

	case Protocol.ARYBLOCKSTORE:
	  if (debug)
	    System.out.println("ARYBLOCKSTORE");
	  {
	    char componentTypecode;
	    int dstpos, len;

	    componentTypecode = in.readChar();
	    dstpos = in.readInt();
	    len = in.readInt();
	    if (debug) {
	      boolean orig = MetaVM.remoteTransparency(false);
	      System.out.println("componentType: " + componentTypecode);
	      System.out.println("position: " + dstpos);
	      System.out.println("length  : " + len);
	      MetaVM.remoteTransparency(orig);
	    }

	    if (!obj.getClass().isArray() /* not array */ ||
		(dstpos < 0) || (dstpos >= this.length)) {
	      out.writeByte(Protocol.ERROR);
	      out.flush();
	    }
	    else {
	      out.writeByte(Protocol.OK);
	      out.flush();

	      // normalize length
	      if (len < 0)  len = Integer.MAX_VALUE;
	      len = Math.min(this.length - dstpos, len);

	      // receive elements
	      int i;
	      switch (componentTypecode) {
	      case 'L':
		Object[] larray = (Object[])this.obj;
		try {
		  for (i = 0; i < len; i++)
		    larray[dstpos + i] = in.readDistObject();
		}
		catch (ClassNotFoundException e) {
		  // not reached
		  boolean orig = MetaVM.remoteTransparency(false);
		  if (debug)  e.printStackTrace();
		  MetaVM.remoteTransparency(orig);
		}
		break;
	      case 'Z':
		boolean[] zarray = (boolean[])this.obj;
		for (i = 0; i < len; i++)
		  zarray[dstpos + i] = in.readBoolean();
		break;
	      case 'B':
		byte[] barray = (byte[])this.obj;
		for (i = 0; i < len; i++)
		  barray[dstpos + i] = in.readByte();
		break;
	      case 'C':
		char[] carray = (char[])this.obj;
		for (i = 0; i < len; i++)
		  carray[dstpos + i] = in.readChar();
		break;
	      case 'S':
		short[] sarray = (short[])this.obj;
		for (i = 0; i < len; i++)
		  sarray[dstpos + i] = in.readShort();
		break;
	      case 'I':
		int[] iarray = (int[])this.obj;
		for (i = 0; i < len; i++)
		  iarray[dstpos + i] = in.readInt();
		break;
	      case 'J':
		long[] jarray = (long[])this.obj;
		for (i = 0; i < len; i++)
		  jarray[dstpos + i] = in.readLong();
		break;
	      case 'F':
		float[] farray = (float[])this.obj;
		for (i = 0; i < len; i++)
		  farray[dstpos + i] = in.readFloat();
		break;
	      case 'D':
		double[] darray = (double[])this.obj;
		for (i = 0; i < len; i++)
		  darray[dstpos + i] = in.readDouble();
		break;
	      }
	    }
	  }
	  break;

	case Protocol.CMDRESET:
	  if (debug)
	    System.out.println("CMDRESET");
	  {
	    this.server.reset();
	  }
	  break;
	}
      }	// while (!done)
    }
    catch (IOException e) {
      if (debug) {
	boolean orig = MetaVM.remoteTransparency(false);
	e.printStackTrace();
	MetaVM.remoteTransparency(orig);
      }
    }
    finally {
      close();
    }
  }
}
