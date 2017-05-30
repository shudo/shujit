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


public class Protocol {
  // requests
  public static final int OK		= 0;
  public static final int ERROR		= 1;
  public static final int CLOSE		= 2;
  public static final int REFERENCE	= 3;

  public static final int NEW		= 10;
  public static final int NEWARRAY	= 11;
  public static final int ANEWARRAY	= 12;
  public static final int MULTIANEWARRAY= 13;

  public static final int INVOKE	= 20;

  public static final int MONITORENTER	= 22;
  public static final int MONITOREXIT	= 23;

  public static final int GET32FIELD	= 30;
  public static final int GET64FIELD	= 31;
  public static final int GETOBJFIELD	= 32;
  public static final int PUT32FIELD	= 33;
  public static final int PUT64FIELD	= 34;
  public static final int PUTOBJFIELD	= 35;

  public static final int ARYLENGTH	= 40;
  public static final int ARYLOAD32	= 41;
  public static final int ARYLOAD64	= 42;
  public static final int ARYLOADOBJ	= 43;
  public static final int ARYSTORE32	= 44;
  public static final int ARYSTORE64	= 45;
  public static final int ARYSTOREOBJ	= 46;

  public static final int GETOBJCOPY	= 50;
  public static final int GETARYCOPY	= 51;
  public static final int ARYBLOCKLOAD	= 52;
  public static final int ARYBLOCKSTORE	= 53;

  public static final int REFREGISTRY	= 60;

  public static final int CMDRESET	= 70;
}
