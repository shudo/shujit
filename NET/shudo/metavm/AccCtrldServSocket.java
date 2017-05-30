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


import java.net.InetAddress;
import java.net.Socket;
import java.net.ServerSocket;
import java.io.IOException;


/**
 * アクセス制御のかかった ServerSocket。
 */
public class AccCtrldServSocket extends ServerSocket {
  private AccessController ac = null;

  public AccCtrldServSocket(AccessController ac, int port)
	throws IOException {
    super(port);
    this.ac = ac;
  }
  public AccCtrldServSocket(AccessController ac, int port, int backlog)
	throws IOException {
    super(port, backlog);
    this.ac = ac;
  }
  public AccCtrldServSocket(AccessController ac, int port, int backlog,
			InetAddress bindAddr) throws IOException {
    super(port, backlog, bindAddr);
    this.ac = ac;
  }


  /**
   * アクセス制御される accept()。overrides ServerSocket#accept()。
   *
   * @see java.net.ServerSocket#accept()
   */
  public Socket accept() throws IOException {
    Socket sock = super.accept();

    if (ac == null)  return sock;

    if (ac.allow(sock))  return sock;
    else {
      sock.close();
      throw new IOException("Connection via " + sock + " is not allowed.");
    }
  }
}
