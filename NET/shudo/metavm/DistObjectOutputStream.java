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


import java.io.ObjectOutputStream;
import java.io.OutputStream;
import java.io.IOException;
import java.io.Serializable;


/**
 * This stream can replace a local reference
 * with a remote reference as a proxy object.
 */
public class DistObjectOutputStream extends ObjectOutputStream {
  private boolean debug;

  public DistObjectOutputStream(OutputStream out)
	throws IOException {
    super(out);
    this.debug = MetaVM.debug;
    super.enableReplaceObject(true);

    out.flush();	// needed if out is buffered
  }

  /**
   * Writes and converts local objects to remote one.
   */
  public void writeDistObject(Object obj) throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);
//    MetaVM.addCheckPassType(java.io.Serializable.class);

if (debug) {
  System.out.println("writeDistObject(): " + obj);
  System.out.flush();
}

    this.writeObject(obj);

    MetaVM.remoteTransparency(orig);
//    MetaVM.clearCheckPassType();
  }


  private boolean onceByValue = false;
  protected void copyNextObject() {
    this.onceByValue = true;
  }


  protected Object replaceObject(Object obj) throws IOException {
    boolean orig = MetaVM.remoteTransparency(false);
if (debug) {
  System.out.println("replaceObject(): " + obj);
  System.out.flush();
}

    if (this.onceByValue) {
      this.onceByValue = false;
      // do not wrap
if (debug) {
  System.out.println(" (replaceObject) do not wrap with Proxy: " + obj);
  System.out.flush();
}
    }
    else if (ByValueUtil.isByValue(obj)) {
if (debug) {
  System.out.println(" (replaceObject) isByValue: " + obj);
  System.out.flush();
}
    }
    else {
      // wrap the local object with a proxy object
      Proxy proxy = Proxy.get(obj);
      obj = proxy;
if (debug) {
  System.out.println(" (replaceObject) to " + obj);
  System.out.flush();
}
    }

    MetaVM.remoteTransparency(orig);

    return obj;
  }
}
