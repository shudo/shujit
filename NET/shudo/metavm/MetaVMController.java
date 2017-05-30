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

import java.net.Socket;
import java.net.UnknownHostException;
import java.io.IOException;
import java.io.BufferedOutputStream;
import java.io.BufferedInputStream;


public class MetaVMController {
  private static final String COMMAND = "metavmctl";

  private void usage(String cmd) {
    System.out.println("usage: " + cmd + "<command> host[:port] ...");
    System.out.println("  command:");
    System.out.println("    reset");
  }

  private void start(String[] args) {
    if (args.length < 1) {
      usage(COMMAND);
      System.exit(1);
    }

    MetaVMCommand commandObj = null;

    String command = args[0].toLowerCase();
    if (command.equals("reset")) {
      commandObj = new CommandReset();
    }
    else {
      System.out.println("Illegal command: " + command);
      usage(COMMAND);
      System.exit(1);
    }

    for (int i = 1; i < args.length; i++) {
      VMAddress addr = null;
      try {
	addr = new VMAddress(args[i]);
      }
      catch (UnknownHostException e) {
	System.out.println("Host name is unknown: " + args[i]);
	continue;
      }

      System.out.println("  target: " + addr);

      Socket sock;
      DistObjectOutputStream out;  DistObjectInputStream in;
      try {
	sock = new Socket(addr.inetAddress(), addr.port());
	out = new DistObjectOutputStream(new BufferedOutputStream(
					sock.getOutputStream()));
	in = new DistObjectInputStream(new BufferedInputStream(
					sock.getInputStream()));

	commandObj.command(out, in);

	sock.close();
      }
      catch (IOException e) {
	System.out.println("An exception is occurred: " + addr);
	e.printStackTrace();
	continue;
      }
    }
  }


  public static void main(String[] args) {
    new MetaVMController().start(args);
  }


  static interface MetaVMCommand {	// a template of commands
    public void command(DistObjectOutputStream out, DistObjectInputStream in)
	throws IOException;
  }

  static class CommandReset implements MetaVMCommand {	// reset command
    public void command(DistObjectOutputStream out, DistObjectInputStream in)
	throws IOException {
      out.writeByte(Protocol.CMDRESET);
      out.flush();
    }
  }
}
