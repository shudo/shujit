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

#include <stdio.h>
#include <unistd.h>	// for lseek(), getopt()
#include <sys/types.h>	// for open(), lseek()
#include <sys/stat.h>	// for open(), gdbm_open()
#include <fcntl.h>	// for open()

#include "compiler.h"

#ifdef GDBM
#  include <gdbm.h>
#else
#  include <ndbm.h>
#endif

#ifndef FILENAME_MAX
#  defne FILENAME_MAX	1023
#endif

#ifndef MIN
#  define MIN(a,b)	((a)<(b)?(a):(b))
#  define MAX(a,b)	((a)>(b)?(a):(b))
#endif


#define COMMAND	"codedbinfo"


void usage(const char *command) {
  printf("usage: %s [-h] [db_file_name]\n", command);
}


int main(int argc, char **argv) {
  int ch;	// for getopt()
  char *fname_db, *fname_page;
#ifdef GDBM
  GDBM_FILE db;
#else
  DBM *db;
#endif
  int db_page = -1;

  datum key, dbdata;


  while ((ch = getopt(argc, argv, "h")) >= 0) {
    switch (ch) {
    case 'h':
      usage(COMMAND);
      exit(0);
      break;
    default:
      break;
    }
  }
  argc -= optind;

  if (argc <= 0) {
    fname_db = CODEDB_DB;
    fname_page = CODEDB_PAGE;
  }
  else {	// file name is specified
    int suf_len = MAX(strlen(CODEDB_DB_SUFFIX), strlen(CODEDB_PAGE_SUFFIX));
    int index;

    fname_db = (char *)malloc(FILENAME_MAX + 1);
    fname_page = (char *)malloc(FILENAME_MAX + 1);

    strncpy(fname_db, argv[1], FILENAME_MAX + 1);
    strncpy(fname_page, argv[1], FILENAME_MAX + 1);

    index = strlen(fname_db) - 1;
    if (fname_db[index] == '.') {
      fname_db[index] = fname_page[index] = '\0';
      index = -1;
    }
    while (index >= 0) {
      if (fname_db[index] == '.')  break;
      index--;
    }
    if (index >= 0) {
      index = MIN(index, FILENAME_MAX + 1 - suf_len);
      fname_page[index] = '\0';
      strcat(fname_page, CODEDB_PAGE_SUFFIX);
    }
    else {
      index = MIN(strlen(fname_db), FILENAME_MAX + 1 - suf_len);
      fname_db[index] = fname_page[index] = '\0';
      strcat(fname_db, CODEDB_DB_SUFFIX);
      strcat(fname_page, CODEDB_PAGE_SUFFIX);
    }
  }

  printf("db   file: %s\npage file: %s\n\n", fname_db, fname_page);


  if ((db_page = open(fname_page, O_RDONLY)) < 0) {
    perror("open");  exit(1);
  }

#ifdef GDBM
  if (!(db = gdbm_open(fname_db, 512, GDBM_READER, 0, NULL))) {
#else
  if (!(db = dbm_open(fname_db, O_RDONLY, 0))) {
#endif
    perror("dbm_open");
    if (db_page >= 0)  close(db_page);
    exit(1);
  }

  free(fname_db);  fname_db = NULL;
  free(fname_page);  fname_page = NULL;


  printf("code_size #pc_entry");
#ifdef EXC_BY_SIGNAL
  printf(" #throw_entry");
#endif
  printf("\n\n");

#ifdef GDBM
  key = gdbm_firstkey(db);
#else
  key = dbm_firstkey(db);
#endif
  while (key.dptr) {
    off_t offset;

#ifdef GDBM
    dbdata = gdbm_fetch(db, key);
#else
    dbdata = dbm_fetch(db, key);
#endif

    offset = *((off_t *)dbdata.dptr);

    if ((lseek(db_page, offset, SEEK_SET)) < 0) {
      perror("lseek");  exit(1);
    }

    {
      int32_t code_size;
      uint32_t pctablelen;
#ifdef EXC_BY_SIGNAL
      uint32_t throwtablelen;
#endif

#define BUF_LEN	256
      char buf[BUF_LEN];
      int len;

      read(db_page, &code_size, 4);
      read(db_page, &pctablelen, 4);
#ifdef EXC_BY_SIGNAL
      read(db_page, &throwtablelen, 4);
#endif

      len = MIN(key.dsize, BUF_LEN - 1);
      strncpy(buf, key.dptr, len);
      buf[len] = '\0';

#ifdef EXC_BY_SIGNAL
      printf("%6d %5d %5d  %s\n", code_size, pctablelen, throwtablelen, buf);
#else
      printf("%6d %5d  %s\n", code_size, pctablelen, buf);
#endif
    }

#ifdef GDBM
    key = gdbm_nextkey(db, key);
#else
    key = dbm_nextkey(db, key);
#endif
  }


  close(db_page);
#ifdef GDBM
  gdbm_close(db);
#else
  dbm_close(db);
#endif


  return 0;
}
