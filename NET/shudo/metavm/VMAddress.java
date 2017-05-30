/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1998,1999,2001 Kazuyuki Shudo

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
import java.net.UnknownHostException;
import java.util.Properties;	// for some properties


/**
 * This class represents a place which a JVM relies on.
 */
public class VMAddress implements ByValue {
  /**
   * JVM が待つデフォルトのポート番号。
   */
  private static int VMPort = 10050;
  protected static int classLoaderPort;
  public static final int CLASSLOADER_PORT_DIFF = 1;

  protected InetAddress ipaddr;
  protected int port;

  private static InetAddress localInetAddress;
  private static VMAddress localVMAddress;

  static {
    boolean orig = MetaVM.remoteTransparency(false);

    Properties props = System.getProperties();
    String prop;

    prop = props.getProperty("metavm.port");
    if (prop != null) {
      int port = Integer.parseInt(prop);
      if (port > 0)  VMAddress.localPort(port);
      else {
	System.out.println(
		"port number must be positive and non-zero number: " + port);
      }
    }

    prop = props.getProperty("metavm.hostname");
    if (prop != null) {
      try {
	localInetAddress = InetAddress.getByName(prop);
      }
      catch (UnknownHostException e) {
	System.err.println("Cannot get IP address for `" + prop + "'.");
	System.exit(1);
      }
    }
    else {
      try {
	localInetAddress = InetAddress.getLocalHost();
      }
      catch (UnknownHostException e) {
	System.err.println("Cannot get local IP address.");
	System.exit(1);
      }

      if (localInetAddress.getHostAddress().equals("127.0.0.1")) {
	System.err.println(
	  "VMAddress: InetAddress.getLocalHost() returns 127.0.0.1...");
      }
    }

    localPort(VMPort);

    MetaVM.remoteTransparency(orig);
  }

  synchronized protected static void localPort(int port) {
    boolean orig = MetaVM.remoteTransparency(false);

    VMPort = port;
    classLoaderPort = VMPort + CLASSLOADER_PORT_DIFF;
    localVMAddress = new VMAddress(localInetAddress, port);

    MetaVM.remoteTransparency(orig);
  }
  protected static int localPort() {
    return VMPort;
  }


  public VMAddress(InetAddress ipaddr, int port) {
    this.ipaddr = ipaddr;
    this.port = port;
  }
  public VMAddress(InetAddress ipaddr) {
    this(ipaddr, VMPort);
  }
  public VMAddress(String host, int port) throws UnknownHostException {
    this.ipaddr = InetAddress.getByName(host);
    this.port = port;
  }
  public VMAddress(String host) throws UnknownHostException {
    boolean orig = MetaVM.remoteTransparency(false);

    String hostpart = host;
    int port = VMPort;

    // to accept hostpart:port
    int colonIndex = host.lastIndexOf(':');
    if (colonIndex >= 0) {
      hostpart = host.substring(0, colonIndex);
      String portpart = host.substring(colonIndex + 1);
      try { port = Integer.parseInt(portpart); }
      catch (NumberFormatException e) { port = VMPort; }
    }

    this.ipaddr = InetAddress.getByName(hostpart);
    this.port = port;

    MetaVM.remoteTransparency(orig);
  }


  public int hashCode() {	// to be improved
    return ipaddr.hashCode() + port * 65535;
  }

  public boolean equals(Object obj) {
    if (obj instanceof VMAddress) {
      VMAddress addr = (VMAddress)obj;
      if (this.ipaddr.equals(addr.ipaddr) && (this.port == addr.port))
	return true;
    }
    return false;
  }

  public String toString() {
    return ipaddr.getHostName() + ":" + port;
  }


  private void writeObject(java.io.ObjectOutputStream out)
	throws java.io.IOException {
    byte[] addr = this.ipaddr.getAddress();
    out.writeByte(addr[0]);  out.writeByte(addr[1]);
    out.writeByte(addr[2]);  out.writeByte(addr[3]);
    out.writeShort(this.port);
  }

  private void readObject(java.io.ObjectInputStream in)
	throws java.io.IOException, ClassNotFoundException {
    int b0, b1, b2, b3;
    b0 = in.readUnsignedByte();  b1 = in.readUnsignedByte();
    b2 = in.readUnsignedByte();  b3 = in.readUnsignedByte();
    String dottedDecimal =
	Integer.toString(b0) + "." + Integer.toString(b1) + "." +
	Integer.toString(b2) + "." + Integer.toString(b3);
    this.ipaddr = InetAddress.getByName(dottedDecimal);
    this.port = in.readUnsignedShort();
  }


  public InetAddress inetAddress() { return this.ipaddr; }
  public int port() { return this.port; }

  public String hostName() { return this.ipaddr.getHostName(); }
  public String hostAddress() { return this.ipaddr.getHostAddress(); }


  /**
   * ローカルアドレスを返す。
   */
  public static VMAddress localAddress() {
    return localVMAddress;
  }

  /**
   * ローカルアドレスかどうか判定する。
   */
  public boolean isLocalAddress() {
    return this.equals(localVMAddress);
  }
}
