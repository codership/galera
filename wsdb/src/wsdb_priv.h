// Copyright (C) 2007 Codership Oy <info@codership.com>

#ifndef WSDB_PRIV_INCLUDED
#define WSDB_PRIV_INCLUDED

#include <stdlib.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <limits.h>

//#ifdef HAVE_MYSQL_DBUG
//#if !defined(HAVE_UINT)
//#undef HAVE_UINT
//#define HAVE_UINT
//typedef unsigned int uint;
//typedef unsigned short ushort;
//#endif
/*
  Support macros for non ansi & other old compilers. Since such
  things are no longer supported we do nothing. We keep then since
  some of our code may still be needed to upgrade old customers.
*/
//#define _VARARGS(X) X
//#define _STATIC_VARARGS(X) X
//#define _PC(X)	X
//xxx
//#define _line_ LINE
//#include "my_dbug.h"
/*#include "my_global.h" */
//#elif defined (HAVE_LIBDBUG)
//#include <dbug.h>
//#else
/* Include empty definitions for DBUG macros */
//#include "no_dbug.h"
//#endif /* HAVE_MYSQL_DBUG */

#include <galerautils.h>
#include "wsdb_api.h"
#include "wsdb_file.h"
#include "hash.h"
#include "key_array.h"
#include "conn.h"
#include "certification.h"
#include "mempool.h"
#include "local.h"

#define LOCAL_CACHE_LIMIT 1000000
#define TRX_LIMIT         USHRT_MAX
#define QUERY_LIMIT       USHRT_MAX
#define CONN_LIMIT        10000

/* make sure 64bit integer limits are defined */
# ifndef LLONG_MIN
#  define LLONG_MIN	(-LLONG_MAX-1)
# endif
# ifndef LLONG_MAX
#  define LLONG_MAX	__LONG_LONG_MAX__
# endif
# ifndef ULLONG_MAX
#  define ULLONG_MAX	(LLONG_MAX * 2ULL + 1)
# endif

#define _MAKE_OBJ(obj, def, size)                    \
{                                                    \
    obj = (struct def *) gu_malloc (size);           \
    if (!obj) {                                      \
        gu_error ("internal error");                 \
        assert(0);                                   \
    }                                                \
    obj->ident = IDENT_##def;                        \
}
#define MAKE_OBJ(obj, def)                           \
{                                                    \
    _MAKE_OBJ(obj, def, sizeof(struct def));         \
}
#define MAKE_OBJ_SIZE(obj, def, extra)               \
{                                                    \
    _MAKE_OBJ(obj, def, sizeof(struct def) + extra); \
}
    

#define CHECK_OBJ(obj, def)                          \
{                                                    \
    if (!obj || obj->ident != IDENT_##def) {         \
        gu_error ("internal error");                 \
        assert(0);                                   \
    }                                                \
}

struct wsdb_info_internal {
	  uint32_t block_count;
	  uint32_t cache_size;
	  uint32_t free_list;
	  struct hash *hash;
	  struct wsdb_file *file;
};

/*! serial representation of write set consist of records.
 * Each record has a type and know length:
 * <rec_type><rec_len><rec>
 * <rec_type>: 1 byte record type flag
 * <rec_len> : 2 bytes unsigned short integer
 * <rec>     : rec_len bytes of the record data
 */

/* each file record has a record type char in front */
#define REC_TYPE_TRX      'T'
#define REC_TYPE_QUERY    'Q'
#define REC_TYPE_ACTION   'A'
#define REC_TYPE_ROW_KEY  'K'
#define REC_TYPE_ROW_DATA 'D'
#define MAX_KEY_LEN 1024

#define MAX_DBTABLE_LEN 256
/* records for representing write set in serial format */
struct file_row_key {
    //uint16_t table_id;
    uint16_t dbtable_len;
    char     dbtable[MAX_DBTABLE_LEN];
    uint16_t key_len;
    char     key[]; //!< key data follows after the struct
};

/* conversions from wsdb_* write set representation to file record format */
struct file_row_key *wsdb_key_2_file_row_key(struct wsdb_key_rec *key);
struct wsdb_key_rec *file_row_key_2_wsdb_key(struct file_row_key *row_key);

uint32_t get_table_id(char *dbtable);

uint16_t serialize_key(char **data, struct wsdb_table_key *key);

struct wsdb_table_key *inflate_key(char *data, uint16_t data_len);
uint16_t serialize_full_key(char **data, struct wsdb_key_rec *key);
uint16_t serialize_all_keys(char **data, struct wsdb_write_set *ws);

#endif
