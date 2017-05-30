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

import java.util.Stack;


public class ThreadPool {
  private static final int INIT_SIZE = 3;
  private static final int MAX_SIZE = 10;

  private ThreadGroup g = new ThreadGroup("Thread pool");
  private Stack stack = new Stack();
  private boolean debug = false;

  private ThreadPoolHook hook = null;


  /**
   * A clazz must be instance of Thread
   * and has default constructor which get no arguments.
   */
  public ThreadPool(ThreadPoolHook hook, boolean debug) {
    this.hook = hook;
    this.debug = debug;

    for (int i = 0; i < INIT_SIZE; i++) {
      stack.push(instantiateAThread());
    }
  }
  public ThreadPool(ThreadPoolHook hook) { this(hook, false); }
  public ThreadPool(boolean debug) { this(null, debug); }
  public ThreadPool() { this(null, false); }


  private final AThread instantiateAThread() {
    AThread t = new AThread(stack, hook, g, "Worker in thread pool");
    t.setDaemon(true);	// daemon thread
    if (hook != null)  hook.newHook(t);
    t.start();
    return t;
  }


  public void run(Runnable target) {
    AThread t = null;

    synchronized (stack) {
      if (stack.empty()) {
	t = instantiateAThread();
      }
      else {
	t = (AThread)stack.pop();
      }
    }

    t.run(target);
  }


  static class AThread extends Thread {
    private ThreadPoolHook hook;
    private Stack stack;

    public AThread(Stack stack, ThreadPoolHook hook,
			ThreadGroup g, String name) {
      super(g, name);
      this.hook = hook;
      this.stack = stack;
    }

    private Runnable target = null;

    public void run(Runnable r) {
      this.target = r;
      synchronized (this) {
	this.notify();
      }
    }

    public void run() {
    mainloop:
      while (true) {
	// wait for Runnable target is filled.
	while (target == null) {
	  try {
	    synchronized (this) {
	      this.wait();
	    }
	  }
	  catch (InterruptedException e) {}
	}

	if (hook != null)  hook.runHook(this);
	try {
	  target.run();
	  if (hook != null)  hook.doneHook(this);
	}
	catch (Exception e) {
	  if (hook != null)  hook.doneHook(this);
		// must be done before printStackTrace()

	  e.printStackTrace();
	}

	this.target = null;

	synchronized (stack) {
	  int size = stack.size();
	  if (size < ThreadPool.MAX_SIZE) {
	    stack.push(this);
	  }
	  else {
	    break mainloop;
	  }
	}
      }	// while (true)

      // commit suicide..
    }
  }
}
