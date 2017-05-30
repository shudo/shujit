/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1998,1999,2000 Kazuyuki Shudo

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

import java.util.Dictionary;
import java.util.Hashtable;
import java.util.Enumeration;


/**
 * A table of exported objects.
 */
public class ExportTable {
  private static boolean debug;
  static {
    debug = MetaVM.debug;
  }


  private ExportTable() {}	// indicates prohibition of instantiate


  private static final int TABLE_SIZE = 1000;

  private static Dictionary table = new Hashtable(TABLE_SIZE);

  protected static void reset() {
    synchronized(table) {
      table = new Hashtable(TABLE_SIZE);
    }
  }

  protected static void register(Object obj) {
    if (obj == null)  return;

    synchronized (table) {
      ExportedObject element = (ExportedObject)table.get(obj);
      if (element != null) {
	element.updateReleaseTime();
      }
      else {
	element = new ExportedObject(obj);
	element.registerTo(table);
      }
    }
  }

  protected static Object get(int id) {
    ExportedObject expdObj = (ExportedObject)table.get(new Integer(id));
    if (expdObj != null)
      return expdObj.target();
    else
      return null;
  }

  protected static void expire() {
    long now = System.currentTimeMillis();
    ExportedObject element;

    synchronized(table) {
      for (Enumeration e = table.elements(); e.hasMoreElements(); ) {
	element = (ExportedObject)e.nextElement();
	if (element.releaseTime() <= now) {
	  element.removeFrom(table);

	  if (debug) {
	    System.out.println("ExportTable#expire(): " + element.target());
	    System.out.flush();
	  }
	}
      }
    }
  }


  /**
   * An exported object registered to ExportTable.
   */
  static class ExportedObject {	// to be inner class of ExportTable
    private Object target;
    private int id;
    private long releaseTime;

    private ExportedObject(Object obj) {
      this.target = obj;
      this.id = ObjectID.idByObject(obj);

      updateReleaseTime();
    }

    private void updateReleaseTime() {
      releaseTime = System.currentTimeMillis() + MetaVM.leasePeriod();
    }

    private Object target() { return this.target; }
    private int id() { return this.id; }
    private long releaseTime() { return this.releaseTime; }

    private void registerTo(Dictionary dic) {
      dic.put(new Integer(this.id), this);
    }
    private void removeFrom(Dictionary dic) {
      dic.remove(new Integer(this.id));
    }
  }
}
