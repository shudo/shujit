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

#include "config.h"

#ifdef CODE_DB

#include <stdio.h>
#include <sys/types.h>	// for lseek()
#include <unistd.h>	// for lseek()

#include "compiler.h"

#ifdef GDBM
#  include <gdbm.h>
#else
#  include <ndbm.h>
#endif


#define MAX_SIG_LEN	256


// in codedb.c
void writeCompiledCode(
#  ifdef GDBM
GDBM_FILE db
#  else
DBM *db
#  endif
, int fd, CompilerContext *cc) {
  struct methodblock *mb = cc->mb;
  datum key, dbdata;
  off_t position;
  char sig[MAX_SIG_LEN];
#ifdef CODE_DB_DEBUG
  printf("writeCompiledCode() called.\n");  fflush(stdout);
#endif

  position = lseek(fd, (off_t)0, SEEK_END);
#ifdef CODE_DB_DEBUG
  printf("  position: %d\n", position);  fflush(stdout);
#endif

  // write to DB
  {
    CodeInfo *info = (CodeInfo *)mb->CompiledCodeInfo;
    pcentry *pcentry = cc->pctable;
#ifdef EXC_BY_SIGNAL
    throwentry *tentry = info->throwtable;
#endif	// EXC_BY_SIGNAL
    uint32_t len;
    int i = 0;

#ifdef CODE_DB_DEBUG
    printf("  code_size:4: 0x%x\n", info->code_size);  fflush(stdout);
#endif
    write(fd, &info->code_size, 4);
    len = pctableLen(cc);
#ifdef CODE_DB_DEBUG
    printf("  pctablelen:4: 0x%x\n", len);  fflush(stdout);
#endif
    write(fd, &len, 4);
#ifdef EXC_BY_SIGNAL
#ifdef CODE_DB_DEBUG
    printf("  throwtablelen:4: 0x%x\n", info->throwtablelen);  fflush(stdout);
#endif
    write(fd, &info->throwtablelen, 4);
#endif	// EXC_BY_SIGNAL

#ifdef CODE_DB_DEBUG
    printf("  exc_handler_nativeoff:4: 0x%x\n", info->exc_handler_nativeoff);
    printf("  finish_return_nativeoff:4: 0x%x\n",
			info->finish_return_nativeoff);
    fflush(stdout);
#endif
    write(fd, &info->exc_handler_nativeoff, 4);
    write(fd, &info->finish_return_nativeoff, 4);

    for (i = 0; i < len; i++) {
      write(fd, &pcentry->opcode, 2);
      write(fd, &pcentry->flag, 2);
      write(fd, &pcentry->operand, 4);
      write(fd, &pcentry->byteoff, 4);
      write(fd, &pcentry->increasing_byteoff, 4);
      write(fd, &pcentry->nativeoff, 4);
      pcentry++;
    }

#ifdef EXC_BY_SIGNAL
    len = info->throwtablelen;
    for (i = 0; i < len; i++) {
      write(fd, &tentry->start, 4);
      write(fd, &tentry->byteoff, 2);
      write(fd, &tentry->len, 1);
#  ifdef PATCH_WITH_SIGTRAP
      write(fd, &tentry->patched_code, 1);
      write(fd, &tentry->cb, 4);
      write(fd, &tentry->opcode, 2);
#  endif
      tentry++;
    }
#endif	// EXC_BY_SIGNAL

    write(fd, mb->CompiledCode, info->code_size);
  }

  // store
  snprintf(sig, MAX_SIG_LEN, "%s#%s%s",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
#ifdef CODE_DB_DEBUG
  printf("  key: %s\n", sig);  fflush(stdout);
#endif

  key.dptr = sig;
  key.dsize = strlen(sig);
  dbdata.dptr = (char *)&position;
  dbdata.dsize = sizeof(off_t);
#ifdef GDBM
  sym_dbm_store(db, key, dbdata, GDBM_REPLACE);
  //sym_dbm_sync(db);
#else
  sym_dbm_store(db, key, dbdata, DBM_REPLACE);
#endif
}


int readCompiledCode(
#  ifdef GDBM
GDBM_FILE db
#  else
DBM *db
#  endif
, int fd, CompilerContext *cc) {
  struct methodblock *mb = cc->mb;
  datum key, dbdata;
  off_t offset;
  char sig[MAX_SIG_LEN];
#ifdef CODE_DB_DEBUG
  printf("readCompiledCode() called.\n");  fflush(stdout);
#endif

  // fetch
  snprintf(sig, MAX_SIG_LEN, "%s#%s%s",
	cbName(fieldclass(&mb->fb)), mb->fb.name, mb->fb.signature);
#ifdef CODE_DB_DEBUG
  printf("  key: %s\n", sig);  fflush(stdout);
#endif

  key.dptr = sig;
  key.dsize = strlen(sig);
  dbdata = sym_dbm_fetch(db, key);

  if (!dbdata.dptr) {	// compiled code is not found
#ifdef CODE_DB_DEBUG
    printf("  code isn't found.\n");  fflush(stdout);
#endif
    return 0;
  }

  offset = *((off_t *)dbdata.dptr);
#ifdef CODE_DB_DEBUG
  printf("  offset: %d\n", offset);  fflush(stdout);
#endif

  if ((lseek(fd, offset, SEEK_SET)) < 0) {
    perror("lseek");
    JVM_Exit(1);
  }

  // read from DB
  {
    CodeInfo *info = (CodeInfo *)mb->CompiledCodeInfo;
    uint32_t len;
    pcentry *pcentry;
#ifdef EXC_BY_SIGNAL
    throwentry *tentry;
#endif	// EXC_BY_SIGNAL
    int i;

    read(fd, &info->code_size, 4);
#ifdef CODE_DB_DEBUG
    printf("  code_size:4: 0x%x\n", info->code_size);  fflush(stdout);
#endif
    read(fd, &len, 4);
    pctableSetLen(cc, len);
#ifdef CODE_DB_DEBUG
    printf("  pctablelen:4: 0x%x\n", pctableLen(cc));  fflush(stdout);
#endif
#ifdef EXC_BY_SIGNAL
    read(fd, &info->throwtablelen, 4);
#ifdef CODE_DB_DEBUG
    printf("  throwtablelen:4: 0x%x\n", info->throwtablelen);  fflush(stdout);
#endif
#endif	// EXC_BY_SIGNAL

    read(fd, &info->exc_handler_nativeoff, 4);
    read(fd, &info->finish_return_nativeoff, 4);
#ifdef CODE_DB_DEBUG
    printf("  exc_handler_nativeoff:4: 0x%x\n", info->exc_handler_nativeoff);
    printf("  finish_return_nativeoff:4: 0x%x\n",
			info->finish_return_nativeoff);
    fflush(stdout);
#endif

    len = pctableLen(cc);
    pctableExtend(cc, len);
    pcentry = cc->pctable;
    for (i = 0; i < len; i++) {
      read(fd, &pcentry->opcode, 2);
      read(fd, &pcentry->flag, 2);
      read(fd, &pcentry->operand, 4);
      read(fd, &pcentry->byteoff, 4);
      read(fd, &pcentry->increasing_byteoff, 4);
      read(fd, &pcentry->nativeoff, 4);
      pcentry++;
    }

#ifdef EXC_BY_SIGNAL
    throwtableExtend(info, info->throwtablelen);
    tentry = info->throwtable;
    len = info->throwtablelen;
    for (i = 0; i < len; i++) {
      read(fd, &tentry->start, 4);
      read(fd, &tentry->byteoff, 2);
      read(fd, &tentry->len, 1);
#  ifdef PATCH_WITH_SIGTRAP
      read(fd, &tentry->patched_code, 1);
      read(fd, &tentry->cb, 4);
      read(fd, &tentry->opcode, 2);
#  endif
      tentry++;
    }
#endif	// EXC_BY_SIGNAL

    mb->CompiledCode = (void *)sysMalloc(info->code_size);
    read(fd, mb->CompiledCode, info->code_size);
  }

  return 1;
}
#endif	// CODE_DB
