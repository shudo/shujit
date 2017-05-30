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

final class ByValueUtil {
  public static boolean isByValue(Object obj) {
    boolean ret = false;

    boolean orig = MetaVM.remoteTransparency(false);

    if ((obj instanceof ByValue) ||
	(obj instanceof Throwable) ||
	(obj instanceof String) ||
	(obj instanceof java.net.InetAddress) ||
	// java.lang
	(obj instanceof Number) ||
	(obj instanceof Boolean) ||
	(obj instanceof Character)
	)
      ret = true;
    else {
      ret = false;
/*
      String cn = obj.getClass().getName();
      if (cn.charAt(0) == '[') {
	char c1 = cn.charAt(1);
	if ((c1 == 'L') || (c1 == '['))
	  ret = true;
      }
*/
    }

    MetaVM.remoteTransparency(orig);

    return ret;
  }

  public static boolean isByValue(Class clz) {
    boolean ret = false;

    boolean orig = MetaVM.remoteTransparency(false);

    if ((ByValue.class.isAssignableFrom(clz)) ||
	(Throwable.class.isAssignableFrom(clz)) ||
	(String.class.isAssignableFrom(clz)) ||
	(java.net.InetAddress.class.isAssignableFrom(clz)) ||
	// java.lang
	(Number.class.isAssignableFrom(clz)) ||
	(Boolean.class.isAssignableFrom(clz)) ||
	(Character.class.isAssignableFrom(clz))
	)
      ret = true;
    else {
      ret = false;
/*
      String cn = clz.getName();
      if (cn.charAt(0) == '[') {
	char c1 = cn.charAt(1);
	if ((c1 == 'L') || (c1 == '['))
	  ret = true;
      }
*/
    }

    MetaVM.remoteTransparency(orig);

    return ret;
  }
}
