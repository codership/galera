// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h> // for getopt
#include <getopt.h> // for getopt_long

#include <errno.h>
#include <ctype.h>

#include "wsdb_priv.h"
#include "hash.h"
#include "version_file.h"

#ifndef TRUE
#define FALSE 0
#define TRUE 1
#endif

int Debug = FALSE;
char *Work_dir  = "/usr/local/mysql/mysql.1";
char *Base_name = "wsdbtrx";

#define PRINT_DBG( arg )     \
    if (Debug) fprintf(stderr, arg );

typedef enum opts
{
    OPT_HELP,
    OPT_DEBUG,
    OPT_DIR,
    OPT_FILE,
    OPT_MAX
}
opt_t;

/* perhaps some short options could be added here */
static struct option options [] =
{
    { "help",  no_argument,       NULL, OPT_HELP      },
    { "debug", required_argument, NULL, OPT_DEBUG      },
    { "dir",   required_argument, NULL, OPT_DIR      },
    { "file",  required_argument, NULL, OPT_FILE      },
    { 0, 0, 0, 0 }
};

/** Prints help message in case of --help option */
static void usage (const char* program_name) {
    printf ("Usage: %s [OPTIONS]\n", program_name);
    printf ("Options are as follows (default values in parentheses):\n");
    printf ("  --help     - print this help info\n");
    printf ("  --debug    - enable debug output\n");
    printf ("  --dir      - directory to look for trx files (%s)\n", Work_dir);
    printf ("  --file     - base name for trx files (%s)\n", Base_name);
    exit (0);
}

int parse (int argc, char* argv[])
{
    int opt = 0;
    int opt_ind = 0;
    int rcode = 0;

    while ((opt = getopt_long (argc, argv, "", options, &opt_ind)) != -1) {
	switch (opt) {
	case '?':
	case OPT_HELP:
	    usage (argv[0]);
	    break;
	case OPT_DIR:
	    Work_dir = optarg;
	    break;
	case OPT_FILE:
	    Base_name = optarg;
	    break;
	case OPT_DEBUG:
	    if (!strcasecmp (optarg, "yes")) {
		Debug = TRUE;
	    }
	    else if (!strcasecmp (optarg, "no")) {
		Debug = FALSE;
	    }
	    else if (!strcasecmp (optarg, "1")) {
		Debug = TRUE;
	    }
	    else if (!strcasecmp (optarg, "0")) {
		Debug = FALSE;
	    }
	    else
		rcode = -1;
	    break;
	default:
	    rcode = -1;
	}
	
	if (errno) {
	    fprintf (stderr,"Error parsing command line option %d:"
		   " option: \'%s\', argument: \'%s\'",
		   opt_ind, options[opt_ind].name, optarg);
	    usage (argv[0]);
	    return errno;
	}
    }

    return errno;
}

struct trx_hdr {
    trx_seqno_t trx_seqno;
};

struct block_info {
    struct wsdb_file *trx_file;
    char             *block;
    uint16_t          block_len;
    char             *pos;
};

static struct wsdb_file *open_version_file(
    const char* work_dir, const char* base_name
) {
    struct wsdb_file *trx_file = version_file_open(
        (Work_dir)  ? Work_dir  : DEFAULT_WORK_DIR,
        (Base_name) ? Base_name : DEFAULT_CERT_FILE,
        DEFAULT_BLOCK_SIZE, DEFAULT_FILE_SIZE
    );

    if (trx_file) {
        trx_file->seek(trx_file, FILE_POS_FIRST, 0);
    }
    return trx_file;
}
#ifdef REMOVED
    memcpy(len_arr, data, 2);
    len_arr[2] = '\0';
    len = atol(len_arr);
    data += 2;
#endif

#ifdef REMOVED
#define GET_VALUE_CNV(data, type, param, cnv)  \
{                                              \
    char *arr = (char *)malloc(sizeof(type) + 1);\
    arr[sizeof(type)] = '\0';                  \
    memcpy(arr, data, sizeof(type));           \
    data += sizeof(type);                      \
    param = cnv(arr);                          \
    free(arr);                                 \
}

#define GET_VALUE_STR(data, len, param )       \
{                                              \
    param = (char *)malloc(len + 1);           \
    param[len] = '\0';                         \
    memcpy(param, data, len);                  \
    data += len;                               \
}

#define GET_VALUE_CHAR(data, param )           \
{                                              \
    param = data[0];                           \
    data += 1;                                 \
}

#define GET_VALUE_STRUCT(data, type, param)    \
{                                              \
    memcpy(param, data, sizeof(type));         \
    data += sizeof(type);                      \
}

#define GET_VALUE_OPAQUE(data, len, param)    \
{                                              \
    memcpy(param, data, len);         \
    data += len;                      \
}
#endif

static int get_data_from_block(
    struct block_info *bi, uint16_t len, void *param
) {
    while(len) {
        int rcode;
        uint16_t read_len = (bi->block + bi->block_len - bi->pos >= len) ?
            len : (bi->block + bi->block_len - bi->pos);
        memcpy(param, bi->pos, read_len);
        len -= read_len;
        bi->pos += read_len;
        if (len) {
            if (Debug) {
              fprintf(stderr, 
                      "reading past block boundary, read_len: %d, len: %d\n",
                      read_len, len);
            }

            param += read_len;
            rcode = bi->trx_file->read_next_block(
                bi->trx_file, bi->block_len, bi->block
            );
            switch (rcode) {
            case WSDB_OK:
                if (Debug) {
                  fprintf(stderr, "next block read\n");
                }
                break;
            case WSDB_ERR_FILE_END:
                fprintf(stderr,
                        "partial block read: %d (read: %d remain: %d)\n",
                        rcode, read_len, len
                );
                break;
            default:
                fprintf(stderr,
                        "unable to read block: %d (read: %d remain: %d)\n",
                        rcode, read_len, len
                );
                return rcode;
            }
            bi->pos = bi->block;
        } else {
          if (Debug) fprintf(stderr,"read len: %d\n", read_len);
        }
    }
    return WSDB_OK;
}

#define GET_VALUE_CHAR(bi, param )           \
{                                              \
    param = data[0];                           \
    data += 1;                                 \
}

#define GET_VALUE_STRUCT(data, type, param)    \
{                                              \
    memcpy(param, data, sizeof(type));         \
    data += sizeof(type);                      \
}

#define GET_VALUE_OPAQUE(data, len, param)    \
{                                              \
    memcpy(param, data, len);         \
    data += len;                      \
}

static int dump_trx(struct block_info *bi) {
    struct trx_hdr trx;
    uint16_t len;
    int rcode;

    if (Debug) fprintf(stderr, "dump_trx begin\n");

    //GET_VALUE_STRUCT(data, uint16_t, &len);
    rcode = get_data_from_block(bi, sizeof(uint16_t), (void *)&len);
    if (rcode != WSDB_OK) return rcode;
    
    if (len != sizeof(struct trx_hdr)) {
        fprintf(stderr, "Bad trx header len: %d\n", len);
        return WSDB_ERR_WS_FAIL;
    }

    //GET_VALUE_STRUCT(data, struct trx_hdr, &trx);
    rcode = get_data_from_block(bi, sizeof(struct trx_hdr), (void *)&trx); 
    if (rcode != WSDB_OK) return rcode;

    fprintf(stdout, "TRX: %llu\n", (unsigned long long)trx.trx_seqno);

    return (WSDB_OK);
}

static int dump_query(struct block_info *bi) {
    uint16_t len;
    char *query;
    int rcode;

    if (Debug) fprintf(stderr, "dump_query begin\n");

    //GET_VALUE_CNV(data, uint16_t, len, atol);
    //GET_VALUE_STRUCT(data, uint16_t, &len);
    rcode = get_data_from_block(bi, sizeof(uint16_t), (void *)&len); 
    if (rcode != WSDB_OK) return rcode;

    //GET_VALUE_STR(data, len, query);
    query = (char *)malloc(len+1);
    query[len] = '\0';

    rcode = get_data_from_block(bi, len, (void *)query);
    if (rcode != WSDB_OK) return rcode;

    fprintf(stdout, "QUERY: %s\n", query);

    free(query);

    return (WSDB_OK);
}

static int dump_action(struct block_info *bi) {
    uint16_t len;
    int rcode;
    struct wsdb_item_rec item;

    if (Debug) fprintf(stderr, "dump_action begin\n");

    //GET_VALUE_CNV(data, uint16_t, len, atol);
    //GET_VALUE_STRUCT(data, uint16_t, &len);
    rcode = get_data_from_block(bi, sizeof(uint16_t), (void *)&len); 
    if (rcode != WSDB_OK) return rcode;

    if (len != 1) {
        fprintf(stderr, "bad action length: %d", len);
        return WSDB_ERR_WS_FAIL;
    }

    //GET_VALUE_CHAR(data, item.action);
    rcode = get_data_from_block(bi, len, (void *)&item.action); 
    if (rcode != WSDB_OK) return rcode;

    fprintf(stdout, "ACTION: %c " , item.action);

    return (WSDB_OK);
}

static int dump_row_key(struct block_info *bi) {
    uint16_t len;
    struct file_row_key *row_key;
    int i = 0, j;
    int rcode;

    if (Debug) fprintf(stderr, "dump_row_key begin\n");

    //GET_VALUE_CNV(data, uint16_t, len, atol);
    //GET_VALUE_STRUCT(data, uint16_t, &len);
    rcode = get_data_from_block(bi, sizeof(uint16_t), &len); 
    if (rcode != WSDB_OK) return rcode;

    row_key = (struct file_row_key *)malloc(len);
    //GET_VALUE_OPAQUE(data, len, row_key);
    rcode = get_data_from_block(bi, len, (void *)row_key); 
    if (rcode != WSDB_OK) return rcode;

    fprintf(stdout, "TABLE: %d ", row_key->dbtable_len);
    for (j = 0; j < row_key->dbtable_len; j++) {
	if (isprint(row_key->dbtable[j]))
	    fprintf(stdout, "%c", row_key->dbtable[j]);
	else
	    fprintf(stdout, ".");
    }
    fprintf(stdout, " KEY: %d", row_key->key_len);
    while(i < row_key->key_len) {
	fprintf(stdout, "%2.2ux", (unsigned char)row_key->key[i++]);
    }
    fprintf(stdout, "\n");

    return (WSDB_OK);
}
static int dump_row_data(struct block_info *bi) {
    if (Debug) fprintf(stderr, "dump_row_data begin\n");
    return (WSDB_OK);
}

static int dump_block(struct block_info *bi) {

    if (Debug) fprintf(stderr, "dump_block begin\n");

    bi->pos = bi->block;
    while (bi->pos < bi->block + bi->block_len) {
        char rec_type;
        int rcode;

        rcode = get_data_from_block(bi, 1, (void *)&rec_type); 
        if (rcode != WSDB_OK) return rcode;

	switch (rec_type) {
	case REC_TYPE_TRX:
            rcode = dump_trx(bi);
	    break;
	case REC_TYPE_QUERY:
            rcode = dump_query(bi);
	    break;
	case REC_TYPE_ACTION:
            rcode = dump_action(bi);
	    break;
	case REC_TYPE_ROW_KEY:
            rcode = dump_row_key(bi);
	    break;
	case REC_TYPE_ROW_DATA:
            rcode = dump_row_data(bi);
	    break;
        case '#':
            if (Debug) fprintf(stderr, "rec type: #\n");
            bi->pos = bi->block + bi->block_len;
            break;
	default:
            fprintf(stderr, "Bad rec type: %c\n", rec_type);
            fprintf(stderr, "at pos: %ld, block len: %d\n", 
                    (long)(bi->pos - bi->block), bi->block_len);
            exit(1);
            break;
	}
        if (rcode) return rcode;
    }
    return WSDB_OK;
}


int dump_trx_files( const char *work_dir, const char *base_name) {
    struct block_info bi;
    int rcode;

    bi.trx_file = open_version_file(work_dir, base_name);
    if (!bi.trx_file) {
        fprintf(stdout,"No wsdb files found\n");
        exit (1);
    }
    bi.block_len = bi.trx_file->get_block_size(bi.trx_file);
    bi.block = (char *)malloc(bi.block_len);

    memset(bi.block, '\0', bi.block_len);

    if (bi.trx_file->read_next_block(bi.trx_file, bi.block_len, bi.block)) {
        fprintf(stderr,"first block read failed\n");
        exit (1);
    }
    while (
        bi.trx_file->read_next_block(
             bi.trx_file, bi.block_len, bi.block) == WSDB_OK) {
      rcode = dump_block(&bi);
      if (rcode) return rcode;
    }

    return (0);
}
int main (int argc, char **argv) {

    parse(argc, argv);

    //work_dir  = (argc > 1 && argv[1]) ? argv[1] : DEFAULT_WORK_DIR;
    //base_name = (argc > 2 && argv[2]) ? argv[2] : DEFAULT_CERT_FILE;

    fprintf(stdout,"Dumping wsdb from: %s/%s\n", Work_dir, Base_name);

    return dump_trx_files(Work_dir, Base_name);
}
