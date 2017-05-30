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

import java.util.Properties;	// for some properties
import NET.shudo.metavm.registry.Registry;


/**
 * An utility class for controlling a behavior of MetaVM.
 */
public final class MetaVM {
  protected static ClassLoader systemClassLoader =
	ClassLoader.getSystemClassLoader();	// cache

  protected static boolean debug = false;
	/* global debug flag */
  protected static int bufsize = 1;
	/* buf. size of Buffered*Stream (in KB) */
  protected static boolean tcp_nodelay = false;
	/* enable/disable TCP_NODELAY (disable/enable Nagle's Algorithm) */
  protected static boolean load_class_locally = false;
	/* always load classes locally */
  protected static AccessController access_controller = null;


  static {
    Properties props = System.getProperties();
    String prop;

    // set debug flag according to property `metavm.debug'.
    prop = props.getProperty("metavm.debug");
    if (prop != null) {
      debug = true;

      System.out.println("debug flag is set.");
    }

    // buf. size of Buffered*Stream
    prop = props.getProperty("metavm.bufsize");
    if (prop != null) {
      int size = Integer.parseInt(prop);
      if (size < 0)  size = 0;
      bufsize = size;
    }
    if (debug)
      System.out.println("buffer size(KB): " + bufsize);

    // TCP_NODELAY
    prop = props.getProperty("metavm.tcp_nodelay");
    if (prop != null) {
      prop = prop.toLowerCase();
      char c;
      try { c = prop.charAt(0); }
      catch (ArrayIndexOutOfBoundsException e) { c = 'n'; }

      if ((c == 'e') || (c == 't') || (c == 'y'))
	tcp_nodelay = true;
    }
    if (debug)
      System.out.println("TCP_NODELAY: " + tcp_nodelay);

    prop = props.getProperty("metavm.load_local");
    if (prop != null) {
      prop = prop.toLowerCase();
      char c;
      try { c = prop.charAt(0); }
      catch (ArrayIndexOutOfBoundsException e) { c = 'n'; }

      if ((c == 'e') || (c == 't') || (c == 'y'))
	load_class_locally = true;
    }
    if (debug)
      System.out.println("load class locally: " + load_class_locally);


    // initialization for MetaVM in native code
    initializationForMetaVM();


    try {
      remoteTransparency(true);	// always to be true
    }
    catch (UnsatisfiedLinkError e) {
      System.err.println("FATAL: MetaVM requires shuJIT (libmetavm.so).");
      e.printStackTrace();
      System.exit(1);
    }
  }


  /**
   * Replaces native methods that are provided by JVM itself.
   * For example, java.lang.System#arraycopy() is replaced.
   */
  private static native void initializationForMetaVM();


  private MetaVM() {}	// indicates prohibition of instantiate


  protected static native boolean remoteTransparency();
  protected static native boolean remoteTransparency(boolean flag);
  protected static native boolean remoteTransparency(Thread t, boolean flag);


  static protected boolean aServerExists = false;

  synchronized public static VMAddress instantiationVM(VMAddress addr) {
    if (!aServerExists) {
      synchronized (MetaVM.class) {
	if (!aServerExists) {
	  aServerExists = true;

	  // invoke a MetaVMServer
	  Thread t = new Thread(new MetaVMServer());
	  t.setName("MetaVM server");
	  t.setDaemon(true);	// daemon thread
	  t.setPriority(Thread.NORM_PRIORITY + 1);
	  t.start();

	  Thread.yield();
		// yield to MetaVM server to determine local port number
	}
      }
    }

    Object origaddr = instantiationVM0(addr);
    if ((addr != null) && (origaddr instanceof VMAddress))
      return (VMAddress)origaddr;
    else
      return null;
  }

  public static VMAddress instantiationVM() {
    return instantiationVM(null);
  }

  private static native Object instantiationVM0(VMAddress addr);


  public static VMAddress addressOf(Object obj) {
    VMAddress ret = null;

    if (obj == null)  return null;

    boolean orig = MetaVM.remoteTransparency(false);

    if (obj instanceof Proxy)
      ret = ((Proxy)obj).address();
    else
      ret = VMAddress.localAddress();

    MetaVM.remoteTransparency(orig);

    return ret;
  }


  public static Registry getRegistry(VMAddress addr)
	throws java.io.IOException {
    return Proxy.getRegistry(addr);
  }


  private static long leasePeriod = 60 * 1000;	// 60 sec.

  /**
   * Sets lease period of references to exported object.
   */
  public static void leasePeriod(long period) {
    leasePeriod = period;
  }
  protected static long leasePeriod() { return leasePeriod; }


  private static long expirationPeriod = 60 * 1000; // 60 sec.

  /**
   * Expiration daemon is activated every at every specified time.
   */
  public static void expirationPeriod(long period) {
    expirationPeriod = period;
  }
  protected static long expirationPeriod() { return expirationPeriod; }
}
