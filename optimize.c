/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 2000,2001,2002 Kazuyuki Shudo

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

#include "compiler.h"
#ifdef METHOD_INLINING
#  include "stack.h"
#endif


#ifdef OPTIMIZE_INTERNAL_CODE
/*
 * Peephole optimization.
 */
void peepholeOptimization(CompilerContext *cc) {
#ifdef COMPILE_DEBUG
  int compile_debug = cc->compile_debug;
#endif
  pcentry *entry;
  int i;

#ifdef COMPILE_DEBUG
  if (compile_debug) {
    struct methodblock *mb = cc->mb;
    printf("peepholeOpt() called: %s#%s %s.\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
    fflush(stdout);
  }
#endif

  for (i = 0; i < pctableLen(cc); i++) {
    pcentry *entry1, *entry2, *entry3, *entry4;
#define GET_1_ENTRY \
	if (!(entry1 = pctableNext(cc, entry)))  break;\
	if (pcentryBlockHead(entry1))  break
#define GET_2_ENTRIES \
	GET_1_ENTRY; \
	if (!(entry2 = pctableNext(cc, entry1)))  break;\
	if (pcentryBlockHead(entry2))  break
#define GET_3_ENTRIES \
	GET_2_ENTRIES; \
	if (!(entry3 = pctableNext(cc, entry2)))  break;\
	if (pcentryBlockHead(entry3))  break
#define GET_4_ENTRIES \
	GET_3_ENTRIES; \
	if (!(entry4 = pctableNext(cc, entry3)))  break;\
	if (pcentryBlockHead(entry4))  break

    entry = cc->pctable + i;

    switch (entry->opcode) {
    case opc_dst:
      // combine FPU store and load instructions
      GET_2_ENTRIES;
      if ((pcentryState(entry) == 0) &&
	  (entry1->opcode == opc_flush_cache) && (entry2->opcode == opc_dld)) {
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  boff:0x%x(%d)\n", entry->byteoff, entry->byteoff);
	  printf("   dst,flush_cache,dld found.\n");
	  fflush(stdout);
	}
#endif
	*entry = *entry1;	// copy opc_flush_cache
	pctableNDelete(cc, i + 1, 2);
      }
      else if ((pcentryState(entry) == 0) &&
       (entry1->opcode == opc_fill_cache) && (entry2->opcode == opc_lstore)) {
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  boff:0x%x(%d)\n", entry->byteoff, entry->byteoff);
	  printf("   dst,fill_cache,dstore %d found.\n", entry2->operand);
	  fflush(stdout);
	}
#endif
	entry->opcode = opc_dst_dstore;
	entry->operand = entry2->operand;
	pctableNDelete(cc, i + 1, 2);
      }
#if !defined(METAVM) || defined(METAVM_NO_ARRAY)
		// the `dst_dastore' insn does not support MetaVM
      else if ((pcentryState(entry) == 0) &&
	       (entry1->opcode == opc_fill_cache) &&
	       (entry2->opcode == opc_lastore)) {
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  boff:0x%x(%d)\n", entry->byteoff, entry->byteoff);
	  printf("   dst,fill_cache,dastore found.\n");
	  fflush(stdout);
	}
#endif
	entry->opcode = opc_dst_dastore;
	pctableNDelete(cc, i + 1, 2);
      }
#endif	// !METAVM
      break;

    case opc_fst:
      GET_1_ENTRY;
      if ((pcentryState(entry) == 0) && (entry1->opcode == opc_istore)) {
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  boff:0x%x(%d)\n", entry->byteoff, entry->byteoff);
	  printf("   fst,fstore %d found.\n", entry1->operand);
	  fflush(stdout);
	}
#endif
	entry->opcode = opc_fst_fstore;
	entry->operand = entry1->operand;
	pctableDelete(cc, i + 1);
      }
#if 0	// single-precision operations need store-reload
	// to be comply with strictfp.
      else {
	GET_2_ENTRIES;
	// combine FPU store and load instructions
	if ((pcentryState(entry) == 0) &&
		 (entry1->opcode == opc_flush_cache) &&
		 (entry2->opcode == opc_fld)) {
#ifdef COMPILE_DEBUG
	  if (compile_debug) {
	    printf("  boff:0x%x(%d)\n", entry->byteoff, entry->byteoff);
	    printf("   fst,flush_cache,fld found.\n");
	    fflush(stdout);
	  }
#endif
	  *entry = *entry1;	// copy opc_flush_cache
	  pctableNDelete(cc, i + 1, 2);
	}
#endif	// if 0
	else {
#if !defined(METAVM) || defined(METAVM_NO_ARRAY)
		// the `fst_fastore' insn does not support MetaVM
	  GET_4_ENTRIES;
	  if ((pcentryState(entry) == 0) && (entry1->opcode == opc_iastore1)) {
#ifdef COMPILE_DEBUG
	    if (compile_debug) {
	      printf("  boff:0x%x(%d)\n", entry->byteoff, entry->byteoff);
	      printf("   fst,...,iastore found.\n");
	      fflush(stdout);
	    }
#endif
	    entry->opcode = opc_fst_fastore;
	    entry->operand = entry4->operand;
	    pctableNDelete(cc, i + 1, 3);
	  }
#endif	// !METAVM
#if 0
	}
#endif	// 0
      }
      break;

    case opc_iload:
      // load a float value to a FPU register directly
      GET_2_ENTRIES;
      if ((entry1->opcode == opc_flush_cache) && (entry2->opcode == opc_fld)) {
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  boff:0x%x(%d)\n", entry->byteoff, entry->byteoff);
	  printf("   fload,flush_cache,fld found.\n");
	  fflush(stdout);
	}
#endif
	entry1->opcode = opc_fload_fld;
	entry1->operand = entry->operand;

	entry->opcode = opc_flush_cache;

	pctableDelete(cc, i + 2);
      }
      break;

    case opc_lload:
      // load a double value to a FPU register directly
      GET_2_ENTRIES;
      if ((entry1->opcode == opc_flush_cache) && (entry2->opcode == opc_dld)) {
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  boff:0x%x(%d)\n", entry->byteoff, entry->byteoff);
	  printf("   dload,flush_cache,dld found.\n");
	  fflush(stdout);
	}
#endif
	entry1->opcode = opc_dload_dld;
	entry1->operand = entry->operand;

	entry->opcode = opc_flush_cache;

	pctableDelete(cc, i + 2);
      }
      break;

    case opc_istore:
      // save wasteful `iload'
      GET_1_ENTRY;
      if ((entry1->opcode == opc_iload) &&
	  (entry->operand == entry1->operand)) {
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  boff:0x%x(%d)\n", entry->byteoff, entry->byteoff);
	  printf("   istore %d,iload %d found.\n",
		entry->operand, entry1->operand);
	  fflush(stdout);
	}
#endif
	entry->opcode = opc_istld;
	pctableDelete(cc, i + 1);
      }
      break;

    case opc_lstore:
      // save wasteful `lload'
      GET_1_ENTRY;
      if ((entry1->opcode == opc_lload) &&
	  (entry->operand == entry1->operand)) {
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  boff:0x%x(%d)\n", entry->byteoff, entry->byteoff);
	  printf("   lstore %d,lload %d found.\n",
		entry->operand, entry1->operand);
	  fflush(stdout);
	}
#endif
	entry->opcode = opc_lstld;
	pctableDelete(cc, i + 1);
      }
      break;

    case opc_iaload:
      GET_2_ENTRIES;
      if ((entry1->opcode == opc_flush_cache) && (entry2->opcode == opc_fld)) {
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  boff:0x%x(%d)\n", entry->byteoff, entry->byteoff);
	  printf("   faload,flush_cache,fld found.\n",
		entry->operand, entry1->operand);
	  fflush(stdout);
	}
#endif
	entry->opcode = opc_faload_fld;
	pctableNDelete(cc, i + 1, 2);
      }
      break;

    case opc_laload:
      GET_2_ENTRIES;
      if ((entry1->opcode == opc_flush_cache) && (entry2->opcode == opc_dld)) {
#ifdef COMPILE_DEBUG
	if (compile_debug) {
	  printf("  boff:0x%x(%d)\n", entry->byteoff, entry->byteoff);
	  printf("   daload,flush_cache,dld found.\n",
		entry->operand, entry1->operand);
	  fflush(stdout);
	}
#endif
	entry->opcode = opc_daload_dld;
	pctableNDelete(cc, i + 1, 2);
      }
      break;
    }
  }

#ifdef COMPILE_DEBUG
  if (compile_debug) {
    printf("peepholeOpt() done.\n");
    fflush(stdout);
  }
#endif
}
#endif	// OPTIMIZE_INTERNAL_CODE


/*
 * Method inlining.
 */
#ifdef METHOD_INLINING
static bool_t methodInlining0(CompilerContext *cc, int loop_depth_threshold);
#endif

void methodInlining(CompilerContext *cc) {
  bool_t inlined;
  int i;

#ifdef METHOD_INLINING
  for (i = 0; i < opt_inlining_depth; i++) {
    inlined = methodInlining0(cc, 0);
    if (!inlined)  break;
  }
#endif

  // set increasing_byteoff
  {
    struct methodblock *mb;
    pcentry *entry;
    int opcode;
    int locked_byteoff = -1;
    int inlined_nest_count;

#ifndef METHOD_INLINING
    for (i = 0; i < pctableLen(cc); i++) {
      entry = cc->pctable + i;
      entry->increasing_byteoff = entry->byteoff;
    }
#else
    inlined_nest_count = 0;
    for (i = 0; i < pctableLen(cc); i++) {
      entry = cc->pctable + i;
      opcode = entry->opcode;

      if (locked_byteoff >= 0)
	entry->increasing_byteoff = locked_byteoff;
      else
	entry->increasing_byteoff = entry->byteoff;

      if (opcode == opc_inlined_enter) {
	if (inlined_nest_count <= 0)
	  locked_byteoff = entry->byteoff;
	inlined_nest_count++;
      }
      else if (opcode == opc_inlined_exit) {
	inlined_nest_count--;
	if (inlined_nest_count <= 0)
	  locked_byteoff = -1;
      }
    }
#endif	// METHOD_INLINING
  }
}


#ifdef METHOD_INLINING
static bool_t methodInlining0(CompilerContext *cc, int loop_depth_threshold) {
  struct methodblock *mb = cc->mb;
  cp_item_type *constant_pool = cbConstantPool(fieldclass(&mb->fb));

  pcentry *entry;
  int opcode, operand, byteoff;
  int i, j;
  Stack *stack = newStack();

  bool_t inlined = FALSE;
  int loop_depth = 0;

#ifdef COMPILE_DEBUG
  if (cc->compile_debug) {
    struct methodblock *mb = cc->mb;
    printf("methodInlining0() called: %s#%s %s.\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
  }
#endif

  i = 0;
  while (i < pctableLen(cc)) {
    entry = cc->pctable + i;
    opcode = entry->opcode;
    operand = entry->operand;
    byteoff = entry->byteoff;

    if (pcentryLoopHead(entry)) {
      loop_depth++;
    }
    if (pcentryLoopTail(entry)) {
      loop_depth--;
      if (loop_depth < 0)  loop_depth = 0;
    }

    switch (opcode) {
    case opc_inlined_enter:
      pushToStack(stack, (long)mb);
      mb = constant_pool[operand].mb;
      constant_pool = cbConstantPool(fieldclass(&mb->fb));
      break;
    case opc_inlined_exit:
      mb = (struct methodblock *)popFromStack(stack);
      constant_pool = cbConstantPool(fieldclass(&mb->fb));
      break;

    case opc_invokespecial:
    case opc_invokestatic:
    case opc_invokestatic_quick:
	// `invokevirtual <private or final method>' is also inlined
	// because it has been translated to `invokespecial' in makePCTable().
      {
	struct methodblock *method = constant_pool[operand].mb;
	CodeInfo *inlined_info;

	if (((method->fb.access & (ACC_NATIVE | ACC_ABSTRACT)) == 0) &&
	    (loop_depth >= loop_depth_threshold)) {
	  inlined_info = (CodeInfo *)(method->CompiledCodeInfo);
	  if (inlined_info == NULL) {
	    if ((inlined_info = prepareCompiledCodeInfo(cc->ee, method))
			== NULL)
	      return FALSE;	// not inlined
	  }

	  if (inlined_info->inlineability == INLINE_UNKNOWN) {
#ifdef COMPILE_DEBUG
	    if (cc->compile_debug) {
	      printf("  compile: %s#%s %s.\n",
		cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature);
	    }
#endif
	    compileMethod(method, STAGE_INTERNAL_CODE);	// make internal codes
	  }

#ifdef COMPILE_DEBUG
	  if (cc->compile_debug) {
	    printf("  inlineability of %s#%s %s: ",
		cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature);
	    switch (inlined_info->inlineability) {
	    case INLINE_UNKNOWN:
	      printf("unknown\n");  break;
	    case INLINE_DONT:
	      printf("dont\n");  break;
	    case INLINE_MAY:
	      printf("may\n");  break;
	    }
	    fflush(stdout);
	  }
#endif
	  if ((inlined_info->inlineability >= INLINE_MAY)
#ifndef USE_SSE2
	      && ((mb->fb.access & ACC_STRICT) ==
			(method->fb.access & ACC_STRICT))
#endif	// USE_SSE2
		) {
	    // do inining
	    pcentry *inlined_pctable;
	    int inlined_pctablelen;
	    int inlined_start, inlined_len;
	    int insert_point;
	    pcentry *inlined_entry;
#ifdef COMPILE_METHOD
	    if (cc->compile_debug) {
	      printf("  inlining: %s#%s %s into %s#%s %s.\n",
		cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature,
		cbName(fieldclass(&mb->fb)),
		mb->fb.name, mb->fb.signature);
	    }
#endif
	    inlined = TRUE;

	    // adjust caller's methodblock->maxstack
	    //   to avoid corruption of JVM stack
	    if (mb->maxstack < method->maxstack)
	      mb->maxstack = method->maxstack;

	    inlined_pctable = inlined_info->pctable;
	    sysAssert(inlined_pctable != NULL);

	    // search for the first internal instruction
	    inlined_pctablelen = inlined_info->pctablelen;
	    for (j = 0; j < inlined_pctablelen; j++) {
	      inlined_entry = inlined_pctable + j;
	      if (inlined_entry->opcode == opc_start) {
		j++;
		inlined_start = j;
		break;
	      }
	    }

	    // count the number of inlined instructions
	    for (; j < inlined_pctablelen; j++) {
	      inlined_entry = inlined_pctable + j;
	      if ((inlined_entry->opcode == opc_end) ||
			(inlined_entry->opcode == opc_return)) {
		inlined_len = j - inlined_start;
		break;
	      }
#ifdef COMPILE_DEBUG
	      if (cc->compile_debug) {
		printf("    inlined: %d\n", inlined_entry->opcode);
	      }
#endif
	    }

	    // insert:
	    //   stateto0, inlined_enter, <inlined insn>, inlined_exit
	    i -= 3;	// points to the `stateto0' insn
	    insert_point = i;
	    pctableNDelete(cc, insert_point, 6);
		// 6: # of internal insn for invoke{special,static}.

	    i += 3 + inlined_len;	// the next value of i

	    pctableInsert(cc, insert_point, opc_inlined_exit, operand,
		byteoff, 0, -1);

	    if ((method->fb.access & ACC_SYNCHRONIZED) &&
		!OPT_SETQ(OPT_IGNLOCK)) {
	      pctableInsert(cc, insert_point,
		(method->fb.access & ACC_STATIC) ?
			opc_sync_static_exit : opc_sync_obj_exit,
		-1, byteoff, 0, -1);
	      i++;
	    }

	    pctableNInsert(cc, insert_point,
		inlined_pctable + inlined_start, inlined_len);

	    if ((method->fb.access & ACC_SYNCHRONIZED) &&
		!OPT_SETQ(OPT_IGNLOCK)) {
	      pctableInsert(cc, insert_point,
		(method->fb.access & ACC_STATIC) ?
			opc_sync_static_enter : opc_sync_obj_enter,
		-1, byteoff, 0, -1);
	      i++;
	    }

#if defined(PATCH_ON_JUMP) && !defined(PATCH_WITH_SIGTRAP) && !defined(INITCLASS_IN_COMPILATION)
	    if (method->fb.access & ACC_STATIC) {
	      pctableInsert(cc, insert_point, opc_init_class, -1,
			byteoff, 0, -1);
	      i++;
	    }
#endif
	    pctableInsert(cc, insert_point, opc_inlined_enter, operand,
		byteoff, 0, -1);
	    pctableInsert(cc, insert_point, opc_stateto0, -1,
		byteoff, 0, -1);

	    continue;	// do not increment i
	  }	// INLINE_MAY && strictfp conditions meet
	}	// not (native or abstract method)
      }		// case invokespecial or invokestatic:
      break;
    }		// switch (opcode)

    i++;
  }	//   while (i < pctableLen(cc))


  freeStack(stack);

#ifdef COMPILE_DEBUG
  if (cc->compile_debug)
    printf("methodInlining0() done.\n");
#endif

  return inlined;
}
#endif	// METHOD_INLINING


#ifdef EAGER_COMPILATION
void eagerCompilation(CompilerContext *cc) {
  struct methodblock *mb = cc->mb;
  cp_item_type *constant_pool = cbConstantPool(fieldclass(&mb->fb));

  pcentry *entry;
  int opcode, operand;
  int i;
#ifdef METHOD_INLINING
  Stack *stack = newStack();
#endif

#ifdef COMPILE_DEBUG
  if (cc->compile_debug) {
    struct methodblock *mb = cc->mb;
    printf("eagerCompilation() called: %s#%s %s.\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
  }
#endif

  for (i = 0; i < pctableLen(cc); i++) {
    entry = cc->pctable + i;
    opcode = entry->opcode;
    operand = entry->operand;

    switch (opcode) {
#ifdef METHOD_INLINING
    case opc_inlined_enter:
      pushToStack(stack, (long)mb);
      mb = constant_pool[operand].mb;
      constant_pool = cbConstantPool(fieldclass(&mb->fb));
      break;
    case opc_inlined_exit:
      mb = (struct methodblock *)popFromStack(stack);
      constant_pool = cbConstantPool(fieldclass(&mb->fb));
      break;
#endif

    case opc_invokespecial:
    case opc_invokestatic:
    case opc_invokestatic_quick:
      {
	struct methodblock *method = constant_pool[operand].mb;
	CodeInfo *info;

	// search opc_invoke_core
	do {
	  i++;
	  entry = cc->pctable + i;
	} while ((entry->opcode != opc_invoke_core) &&
		(entry->opcode != opc_invoke_core_compiled));
	opcode = entry->opcode;
	// operand = entry->operand;

	info = (CodeInfo *)method->CompiledCodeInfo;
	if (info == NULL) {
	  if ((info = prepareCompiledCodeInfo(cc->ee, method)) == NULL)
	    continue;
	}

	if ((method->fb.access & (ACC_ABSTRACT | ACC_NATIVE)) == 0) {
	  CompilerContext *method_cc = info->cc;
	  if ((method_cc == NULL) ||
		(method_cc->stage < STAGE_INTERNAL_CODE)) {
		// prevent self compilation
#ifdef COMPILE_DEBUG
	    if (cc->compile_debug)
	      printf("  %d: compile %s#%s %s.\n", i,
		cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature);
#endif
	    compileMethod(method, STAGE_DONE);
	  }

	  if (method->invoker == sym_invokeJITCompiledMethod) {	// compiled
	    // substitute invoke_core_compiled for invoke_core
#ifdef COMPILE_DEBUG
	    if (cc->compile_debug)
	      printf("  compiled: %s#%s %s.\n",
		cbName(fieldclass(&method->fb)),
		method->fb.name, method->fb.signature);
#endif
	    entry->opcode = opc_invoke_core_compiled;
	  }
	}	// not (abstract or native)
      }
      break;
    }	// switch (opcode)
  }


#ifdef COMPILE_DEBUG
  if (cc->compile_debug) {
    struct methodblock *mb = cc->mb;
    printf("eagerCompilation() done: %s#%s %s.\n",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
  }
#endif
}
#endif	// EAGER_COMPILATION
