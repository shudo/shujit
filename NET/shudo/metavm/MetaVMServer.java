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
import java.net.ServerSocket;
import java.io.IOException;
import NET.shudo.metavm.registry.Registry;

// for ClassDistributionDaemon
//import java.net.Socket;
//import java.net.ServerSocket;
//import java.io.IOException;
import java.util.Dictionary;
import java.util.Hashtable;

// for ClassDistributor
//import java.net.Socket;
//import java.io.IOException;
import java.io.DataOutputStream;
import java.io.DataInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedInputStream;
//import java.util.Dictionary;



/**
 * This is MetaVM server which accepts incoming connection
 * and invokes request server.
 */
public class MetaVMServer implements Runnable {
  private static final String PROGRAM = "metavm";

  /** debug flag */
  private boolean debug = false;
  /** quiet flag */
  private boolean quiet = false;

  private ClassDistributionDaemon clzDaemon;

  protected Registry registry = new Registry();

  /** thread pool */
  private ThreadPool pool = new ThreadPool(
    new ThreadPoolHook() {
      public void runHook(Thread t) { MetaVM.remoteTransparency(t, true); }
      public void doneHook(Thread t) { MetaVM.remoteTransparency(t, false); }
      public void newHook(Thread t) {}
    },
    MetaVM.debug
  );


  protected MetaVMServer() {}


  private void usage() {
    System.out.println("Usage: " + this.PROGRAM + " [OPTION]...\n");

    System.out.println("Start a MetaVM server.");

    System.out.println("  --help:\t\tshow this help message.");
    System.out.println("  -d, --debug:\t\tfor debug.");
    System.out.println("  -q, --quiet:\t\tquiet.");
    System.out.println("  -p, --port <num>:\tspecify port number. (default: 10050)");
    System.out.println("  -h, --hostname <string>:\tset the hostname.");
    System.out.println("  -a, --access <file name>:\tspecify access control file.");
    System.out.println("  -n, --tcp-nodelay:\tset TCP_NODELAY to socket.");
    System.out.println("  -b, --buffer-size <num>:\tspecify buffer size of BufferedStream in KB. (default: 1)");
    System.out.println("  --local-class:\tload all classes from local disk.");

    System.out.println();
  }


  private void start(String[] args) {
    // load the class MetaVM
    MetaVM.aServerExists = true;

    // parse options
    for (int i = 0; i < args.length; i++) {
      if (args[i].equals("-d") || args[i].equals("--debug")) {
	MetaVM.debug = true;
      }
      else if (args[i].equals("-h") || args[i].equals("--hostname")) {
	String s = null;
	try { s = args[++i]; }
	catch (ArrayIndexOutOfBoundsException e) {
	  System.out.println("--hostname option requires an argument.");
	  break;
	}

	System.getProperties().setProperty("metavm.hostname", s);
	if (debug)
	  System.out.println("host name: " + s);
      }
      else if (args[i].equals("-p") || args[i].equals("-port")) {
	String s = null;
	try { s = args[++i]; }
	catch (ArrayIndexOutOfBoundsException e) {
	  System.out.println("-p option requires an argument.");
	  break;
	}

	System.getProperties().setProperty("metavm.port", s);
	if (debug)
	  System.out.println("port number: " + s);
      }
      else if (args[i].equals("-a") || args[i].equals("--access")) {
	String s = null;
	try { s = args[++i]; }
	catch (ArrayIndexOutOfBoundsException e) {
	  System.out.println("-a option requires an argument.");
	  break;
	}

	System.getProperties().setProperty("metavm.access", s);
	if (debug)
	  System.out.println("access control file: " + s);

	try {
	  AccessController ac = new AccessController(s);
	  MetaVM.access_controller = ac;
	}
	catch (IOException e) {
	  System.out.println("reading access control file failed: " + s);
	  e.printStackTrace();
	}
      }
      else if (args[i].equals("-n") || args[i].equals("--tcp-nodelay")) {
	MetaVM.tcp_nodelay = true;
      }
      else if (args[i].equals("-b") || args[i].equals("--buffer-size")) {
	String s = null;
	try { s = args[++i]; }
	catch (ArrayIndexOutOfBoundsException e) {
	  System.out.println("-b option requires an argument.");
	  break;
	}

	int size = Integer.parseInt(s);
	if (size < 0)  size = 0;
	MetaVM.bufsize = size;
	if (debug)
	  System.out.println("buffer size(KB): " + MetaVM.bufsize);
      }
      else if (args[i].equals("--local-class")) {
	MetaVM.load_class_locally = true;
      }
      else if (args[i].equals("-q") || args[i].equals("--quiet")) {
	this.quiet = true;
      }
      else if (args[i].equals("--help") || args[i].equals("-?")) {
	usage();
	System.exit(0);
      }
      else if (args[i].startsWith("-")) {
	System.out.println("invalid option: " + args[i]);
	usage();
	System.exit(1);
      }
    }

    run();
  }


  public void run() {
    // set debug flag of this instance
    this.debug = MetaVM.debug;

    // invoke daemons
    Runnable r;
    Thread t;

    // invoke ExpirationDaemon
    r = new Runnable() {
	public void run() {
	  while (true) {
	    try { Thread.sleep(MetaVM.expirationPeriod()); }
	    catch (InterruptedException e) {}

	    ExportTable.expire();
	  }
	}
      };
    t = new Thread(r, "Expiration Daemon");
    t.setDaemon(true);	// daemon thread
    try {
      t.setPriority(Thread.MIN_PRIORITY);
    } catch (IllegalArgumentException e) {}
    t.start();


    // create a server socket
    ServerSocket servsock = null;
    int port = VMAddress.localPort();
    int retry = 10;

    while (retry > 0) {
      AccessController ac = MetaVM.access_controller;
      try {
	if (ac != null)
	  servsock = new AccCtrldServSocket(ac, port);
	else
	  servsock = new ServerSocket(port);	// throws an exception

	if (debug) {
	  System.out.println("MetaVMServer: port no. is " + port);
	  System.out.flush();
	}

	VMAddress.localPort(port);
	break;
      }
      catch (IOException e) {
	if (debug)  e.printStackTrace();
	if (e instanceof java.net.BindException) {
	  if (debug) {
	    System.out.println("MetaVMServer port " + port + "/tcp is already in use.");
	    System.out.flush();
	  }
	  port++;  retry--;
	}
	else
	  System.exit(1);
      }
    }


    // invoke ClassDistributionDaemon
    r = clzDaemon = new ClassDistributionDaemon();
    t = new Thread(r, "Class Distribution Daemon");
    t.setDaemon(true);	// daemon thread
    try {
      t.setPriority(Thread.currentThread().getPriority() + 1);
    } catch (IllegalArgumentException e) {}
    t.start();


    // start up message
    if (!quiet) {
      System.out.print("  MetaVM server started on ");
      System.out.print(VMAddress.localAddress());
      System.out.println(".");
      System.out.flush();
    }


    // main loop
    while (true) {
      Socket sock = null;

      try {
	sock = servsock.accept();
	if (debug) {
	  System.out.println("MetaVMServer: accept.");
	}

	if (MetaVM.tcp_nodelay)
	  sock.setTcpNoDelay(true);
      }
      catch (Exception e) {
	if (debug)  e.printStackTrace();

	// to avoid busy loop
	try { Thread.sleep(2000); }
	catch (InterruptedException ie) {}

	continue;
      }

      Skeleton reqserv = null;
      try { reqserv = new Skeleton(this, sock); }
      catch (IOException e) {
	// not reached.
	System.err.println("initialization of Skeleton is failed.");
	continue;
      }

      pool.run(reqserv);
    }	// while (true)
  }


  protected void reset() {
    ExportTable.reset();
    RemoteClassLoader.reset();
    this.clzDaemon.reset();

    this.registry.reset();

    System.gc();
  }


  public static void main(String[] args) {
//    System.runFinalizersOnExit(true);

    try {
      new MetaVMServer().start(args);
    }
    catch (UnsatisfiedLinkError e) {
      System.err.println("FATAL: MetaVM requires shuJIT (libmetavm.so).");
      e.printStackTrace();
      System.exit(1);
    }
  }


  /**
   * This daemon accepts incoming connections and invoke ClassDistributors.
   *
   * @see NET.shudo.metavm.ClassDistributor
   */
  public static
  class ClassDistributionDaemon implements Runnable {
    private Dictionary classfileTable = new Hashtable();
    private boolean debug;


    protected ClassDistributionDaemon() {
      this.debug = MetaVM.debug;
    }


    protected void reset() {
      synchronized (classfileTable) {
	classfileTable = new Hashtable();
      }
    }


    public void run() {
      ServerSocket servsock = null;
      int distributorCount = 0;

      if (debug) {
	System.out.println("ClassDistDaemon: port no. is " +
				VMAddress.classLoaderPort);
	System.out.flush();
      }

      AccessController ac = MetaVM.access_controller;
      try {
	if (ac != null)
	  servsock = new AccCtrldServSocket(ac, VMAddress.classLoaderPort);
	else
	  servsock = new ServerSocket(VMAddress.classLoaderPort);
      }
      catch (IOException e) { e.printStackTrace();  System.exit(1); }

      // main loop
      while (true) {
	// accept
	Socket sock = null;

	try {
	  sock = servsock.accept();
	}
	catch (IOException e) {
	  if (debug)  e.printStackTrace();

	  // to avoid busy loop
	  try {  Thread.sleep(2000);  }
	  catch (InterruptedException ie) {}

	  continue;
	}
	if (debug) {
	  System.out.println("ClassDistributionDaemon accepted");
	  System.out.flush();
	}


	distributorCount++;

	Runnable distributor = null;
	try {
	  distributor = new ClassDistributor(sock, classfileTable);
	}
	catch (IOException e) {
	  distributor = null;
	  if (debug)  e.printStackTrace();
	  continue;
	}
	Thread t = new Thread(distributor,
			"Class Distributor " + distributorCount);
	t.start();
      }	// while (true)
    }


    /**
     * An object of this class distributes class definitions.
     *
     * @see NET.shudo.metavm.RemoteClassLoader
     */
    public static
    class ClassDistributor implements Runnable {
      private Socket sock = null;
      private Dictionary classfileTable = null;
      private boolean debug;

      private DataOutputStream out;
      private DataInputStream in;


      protected ClassDistributor(Socket sock, Dictionary classfileTable)
		throws IOException {
	this.sock = sock;
	this.classfileTable = classfileTable;
	this.debug = MetaVM.debug;

	out = new DataOutputStream(new BufferedOutputStream(
					sock.getOutputStream()));
	in = new DataInputStream(new BufferedInputStream(sock.getInputStream()));
      }


      public void run() {
	while (true) {
	  try {
	    String classname;
	    byte[] classfile;

	    // receive class name
	    classname = in.readUTF();
	    if (debug) {
	      System.out.println("ClassDistributor classname: " + classname);
	      System.out.flush();
	    }

	    synchronized(classfileTable) {
	      classfile = (byte[])classfileTable.get(classname);
	    }
	    if (classfile == null) {
	      classfile = LocalClassLoader.loadClassfileLocally(classname);
	      if (classfile == null || classfile.length <= 0) {
		out.writeInt(-1);
		out.flush();
		continue;
	      }

	      synchronized(classfileTable) {
		classfileTable.put(classname, classfile);
	      }
	    }

	    if (debug) {
	      System.out.println("ClassDistributor length: " +
			classfile.length);
	      System.out.flush();
	    }
	    out.writeInt(classfile.length);
	    out.write(classfile);
	    out.flush();
	  }
	  catch (IOException e) {
	    if (debug) {
	      System.out.println("ClassDistributor an exc. occurred:");
	      e.printStackTrace();
	    }
	    break;
	  }
	}	// while (true)
      }
    }	// class ClassDistributor
  }	// class ClassDistributionDaemon
}
