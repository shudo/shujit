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

package NET.shudo.metavm.registry;

import java.util.Dictionary;
import java.util.Hashtable;
import java.util.Enumeration;
import java.util.Vector;


public final class Registry {
  private Dictionary table;

  public Registry() { this.reset(); }


  public void reset() {
    table = new Hashtable();
  }


  public void bind(String name, Object obj) throws AlreadyBoundException {
    if (table.get(name) != null)
      throw new AlreadyBoundException(name);

    table.put(name, obj);
  }


  public void rebind(String name, Object obj) {
    table.put(name, obj);
  }


  public void unbind(String name) throws NotBoundException {
    Object old = table.remove(name);
    if (old == null)
      throw new NotBoundException(name);
  }


  public Object lookup(String name) throws NotBoundException {
    Object obj = table.get(name);
    if (obj == null)
      throw new NotBoundException(name);
    return obj;
  }


  public String[] list() {
    String[] ary = null;
    Enumeration e = table.keys();

    if (e.hasMoreElements()) {
      Vector vec = new Vector();
      do {
	vec.add(e.nextElement());
      } while (e.hasMoreElements());

      int size = vec.size();
      ary = new String[size];
      for (int i = 0; i < size; i++)
	ary[i] = (String)vec.get(i);
    }

    return ary;
  }
}
