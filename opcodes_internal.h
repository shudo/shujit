/*
  This file is part of shuJIT,
  Just In Time compiler for Sun Java Virtual Machine.

  Copyright (C) 1996,1997,1998,1999,2000,2003 Kazuyuki Shudo

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

//
// Internal Opcodes
//
#define	opc_methodhead	230	// 0xe6
#define opc_start	231
#define opc_end		232
#define	opc_epilogue	233
#define	opc_exc_handler	234
#define	opc_methodtail	235
#define	opc_sync_obj_enter	236
#define	opc_sync_obj_exit	237
#define	opc_sync_static_enter	238
#define	opc_sync_static_exit	239

#define opc_inv_head	240	// 0xf0
#define opc_invoke_core	241
#define opc_invoke_core_compiled	242
#define opc_inv_metavm	243

#define opc_inv_vir_obj		244	// 0xf4
#define opc_inv_vir_varspace	245
#define opc_invokevirtual_obj	246

#define opc_inv_spe_obj		247
#define opc_inv_spe_varspace	248

#define opc_inv_stq_obj		249
#define opc_inv_stq_varspace	250

#define	opc_invokeignored_static	251	// 0xfb
#define	opc_invokeignored_static_quick	252

#define opc_exc_check	253

// 254 and 255 are reserved for VM specific instructions
#define	opc_impdep1	254	// 0xfe
#define	opc_impdep2	255	// 0xff

#define	opc_fill_cache	256	// 0x100
#define	opc_flush_cache	257
#define	opc_array_check	258

#define	opc_throw_illegalaccess	259
#define	opc_throw_instantiation	260
#define	opc_throw_noclassdef	261
#define	opc_throw_nofield	262
#define	opc_throw_nomethod	263

#define	opc_strict_enter	264
#define	opc_strict_exit		265

#define	opc_fld4	266	// 0x10a
#define	opc_fld		267
#define	opc_fst		268
#define	opc_dld8	269
#define	opc_dld		270
#define	opc_dst		271

#define	opc_fppc_save		272	// 0x110
#define	opc_fppc_restore	273
#define	opc_fppc_single		274
#define	opc_fppc_double		275
#define	opc_fppc_extended	276

#define	opc_strict_fprep	277	// 0x115
#define	opc_strict_fscdown	278
#define	opc_strict_fscup	279
#define	opc_strict_fsettle	280
#define	opc_strict_dprep	281
#define	opc_strict_dscdown	282
#define	opc_strict_dscup	283
#define	opc_strict_dsettle	284

#define	opc_getstatic2	285	// 0x11d
#define	opc_putstatic2	286
#define	opc_getfield2	287
#define	opc_putfield2	288

#define	opc_iastore1	289	// 0x121
#define	opc_lastore1	290

#define	opc_stateto0	291	// 0x123
#define	opc_stateto1	292
#define	opc_stateto2	293
#define	opc_stateto3	294
#define	opc_stateto4	295

#define	opc_goto_st0	296	// 0x128
#define	opc_goto_st1	297
#define	opc_goto_st2	298
#define	opc_goto_st3	299
#define	opc_goto_st4	300

// for OPTIMIZE_INTERNAL_CODE
#define	opc_fload_fld	301	// 0x12d
#define opc_dload_dld	302
#define	opc_faload_fld	303
#define	opc_daload_dld	304	// 0x130

#define opc_fst_fstore	305
#define opc_dst_dstore	306
#define opc_fst_fastore	307
#define opc_dst_dastore	308

#define opc_istld	309	// 0x135
#define opc_lstld	310

#define opc_invoke_recursive	311
#define opc_invoke_recursive_1	312
#define opc_invoke_recursive_2	313
#define opc_invoke_recursive_3	314

// for method inlining
#define opc_inlined_enter	315	// 0x13b
#define opc_inlined_exit	316

#define opc_init_class	317

// for MetaVM
#define opc_metavm_init	318

// for special inlining
#define	opc_sqrt	319	// 0x13f
#define	opc_sin		320
#define	opc_cos		321
#define	opc_tan		322
#define	opc_atan2	323
#define	opc_atan	324
#define	opc_log		325
#define	opc_floor	326
#define	opc_ceil	327
#define opc_abs_int	328
#define opc_abs_long	329
#define opc_abs_float	330
#define opc_abs_double	331
#define	opc_exp		332
#define opc_asin	333
#define opc_acos	334

#define opc_java_io_bufferedinputstream_ensureopen	335

#define	NOPCODES	336
