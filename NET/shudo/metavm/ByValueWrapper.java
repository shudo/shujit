/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 2001 Kazuyuki Shudo

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


class ByValueWrapper implements ByValue {
  transient private Object target = null;

  protected ByValueWrapper(Object obj) { this.target = obj; }

  protected Object target() { return this.target; }

  private void writeObject(java.io.ObjectOutputStream out)
	throws java.io.IOException {
    DistObjectOutputStream dout = (DistObjectOutputStream)out;

    dout.copyNextObject();

    dout.writeDistObject(target);
  }

  private void readObject(java.io.ObjectInputStream in)
	throws java.io.IOException, ClassNotFoundException {
    DistObjectInputStream din = (DistObjectInputStream)in;
    target = din.readDistObject();
  }
}
