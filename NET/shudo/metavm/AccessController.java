/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1996,1997,1998,2001 Kazuyuki Shudo

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


import java.io.Reader;
import java.io.IOException;
import java.io.File;
import java.io.FileReader;
import java.io.FileNotFoundException;
import java.io.BufferedReader;
import java.net.InetAddress;
import java.net.Socket;
import java.net.UnknownHostException;
import java.util.Vector;
import java.util.Enumeration;
import java.util.StringTokenizer;


/**
 * ネットワーク経由のアクセスを制御するクラス。<BR>
 * {allow,deny} [{hostname,IPaddress}[/netmask][:port] [username]]<BR>
 * 現在、ポート番号は使っていない。
 */
public class AccessController {
  public static final boolean DEFAULT_POLICY = true;	// true means allow


  private AccessControlEntry[] restrictionTable;
  private boolean debug = false;


  public AccessController(String filename) throws IOException {
    this(filename, false);
  }
  public AccessController(File file) throws IOException {
    this(file, false);
  }
  public AccessController(Reader in) throws IOException {
    this(in, false);
  }
  public AccessController(String filename, boolean debug) throws IOException {
    this(new FileReader(filename), debug);
  }
  public AccessController(File file, boolean debug) throws IOException {
    this(new FileReader(file), debug);
  }
  public AccessController(Reader in, boolean debug) throws IOException {
    this.debug = debug;

    Vector table = new Vector();

    BufferedReader din = new BufferedReader(in);
    String aLine;

    while ((aLine = din.readLine()) != null) {
      StringTokenizer tokenizer = new StringTokenizer(aLine, " ,\t\n\r");
      int numToken = tokenizer.countTokens();
      if (numToken <= 0)  continue;	// 空行
      String[] args = new String[numToken];
      int numArgs = args.length;

      int index = 0;
      while (tokenizer.hasMoreElements()) {
	args[index++] = (String)tokenizer.nextElement();
      }


      // 解析
      boolean allow;
      InetAddress address = null;
      int netmask = 0;
      int port = 0;
      String user = null;

      if (args[0].startsWith("#") || args[0].startsWith(";"))	// コメント行
	continue;

      // allow or deny
      if (args[0].toLowerCase().equals("allow")) {
	allow = true;
      }
      else if (args[0].toLowerCase().equals("deny")) {
	allow = false;
      }
      else {
	throw new IOException("Invalid description: " + args[0]);
      }


      // hostname[/netmask][:port]
      if (numArgs >= 2) {
	// :portno
	int colonIndex = args[1].lastIndexOf(':');
	if (colonIndex >= 0) {
	  int digitIndex = colonIndex + 1;
	  int lastIndex = digitIndex;
	  char ch;
	  while (lastIndex < args[1].length()) {
	    ch = args[1].charAt(lastIndex);
	    if (!Character.isDigit(ch))  break;
	    lastIndex++;
	  }
	  if (lastIndex > digitIndex) {	// digits found
	    try {
	      port =
		Integer.parseInt(args[1].substring(digitIndex, lastIndex));
	    }
	    catch (NumberFormatException e) { /* ignore */ }
	  }
	}

	// /netmask
	int slashIndex = args[1].lastIndexOf('/');
	if (slashIndex >= 0) {
	  int digitIndex = slashIndex + 1;
	  int lastIndex = digitIndex;
	  char ch;
	  while (lastIndex < args[1].length()) {
	    ch = args[1].charAt(lastIndex);
	    if (!Character.isDigit(ch))  break;
	    lastIndex++;
	  }
	  if (lastIndex > digitIndex) {	// digits found
	    try {
	      netmask =
		Integer.parseInt(args[1].substring(digitIndex, lastIndex));
	    }
	    catch (NumberFormatException e) { /* ignore */ }
	  }
	}

	// hostname or IP address
	colonIndex = ((colonIndex < 0) ? Integer.MAX_VALUE : colonIndex);
	slashIndex = ((slashIndex < 0) ? Integer.MAX_VALUE : slashIndex);
	int hostLastIndex = Math.min(colonIndex, slashIndex);

	String hostpart;
	if (hostLastIndex < Integer.MAX_VALUE)
	  hostpart = args[1].substring(0, hostLastIndex);
	else
	  hostpart = args[1];
	address = InetAddress.getByName(hostpart);
      }	// if (numArgs >= 2)


      // user
      if (numArgs >= 3)  user = args[2];


      AccessControlEntry entry = new AccessControlEntry(allow,
					address, netmask, port, user);

      table.addElement(entry);
    }

    this.restrictionTable = new AccessControlEntry[table.size()];
    table.copyInto(this.restrictionTable);
  }


  /**
   * 接続を認証する。
   */
  public boolean allow(Socket sock) {
    // IP address を取得
    InetAddress addr = sock.getInetAddress();

    // ユーザID を取得
    SocketAuthenticator id = null;
    String user = null;
    try {
      id = new SocketAuthenticator(sock);
    }
    catch (IOException e) { /* fail */ }
    if (id != null)
      user = id.userID();

    return allow(addr, user);
  }


  /**
   * 接続を認証する。
   */
  public boolean allow(InetAddress addr, String user) {
    boolean result = DEFAULT_POLICY;

    // 認証
    
    for (int i = restrictionTable.length - 1; i >= 0; i--) {
      AccessControlEntry entry = restrictionTable[i];
      if (entry.suits(addr, user)) {
if (debug)
System.out.println(entry + " [suits] " + addr.getHostAddress()
+ ((user != null) ? (" " + user) : ""));
	result = entry.allow;
	break;
      }
      else {
if (debug)
System.out.println(entry + " [doesn't suits] " + addr.getHostAddress()
+ ((user != null) ? (" " + user) : ""));
      }
    }

    return result;
  }


  /**
   * アクセス制御表の一エントリ。
   */
  static class AccessControlEntry {
    protected boolean allow = false;

    private InetAddress address;	// suits any target if null
    private byte[] byteOfAddress;
    private int netmask;
    private int port = 0;

    private String user = null;


    protected AccessControlEntry(boolean allow,
		InetAddress addr, int netmask, int port, String user) {
      // allow or deny
      this.allow = allow;
      // address
      this.address = addr;
      if (addr != null) {
	this.byteOfAddress = addr.getAddress();
	// netmask
	if (netmask <= 0)
	  this.netmask = this.byteOfAddress.length * 8;
	else
	  this.netmask = netmask;
	// port
	if (port <= 0)
	  this.port = 0;	// 0 suits any port number;
	else
	  this.port = port;
      }
      // user
      this.user = user;
    }


    public String toString() {
      StringBuffer buf = new StringBuffer();
      buf.append(this.allow ? "allow" : "deny");
      if (address != null) {
	buf.append(" ");
	buf.append(address.getHostAddress());
	if (byteOfAddress.length * 8 != netmask) {
	  buf.append("/");  buf.append(netmask);
	}
	if (port != 0) {
	  buf.append(":");  buf.append(port);
	}
      }
      if (user != null) {
	buf.append(" ");
	buf.append(user);
      }

      return buf.toString();
    }


    protected boolean suits(InetAddress addr, String user) {
      // address
      if (this.address == null)  return true;

      byte[] tgtBytes = addr.getAddress();

      int fragments = netmask % 8;
      int bytes = netmask / 8;

      if (fragments != 0) {
	byte mask = (byte)(0xff << (8 - fragments));
	if ((tgtBytes[bytes] & mask) != (byteOfAddress[bytes] & mask))
	  return false;
      }
      for (int i = bytes - 1; i >= 0; i--) {
	if (tgtBytes[i] != byteOfAddress[i])
	  return false;
      }

      // user
      if (this.user != null)
	if (!this.user.equals(user))  return false;

      return true;
    }
  }	// class AccessControlEntry
}
