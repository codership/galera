// Copyright (C) 2007 Codership Oy <info@codership.com>

#ifndef WSDB_FILE_API_INCLUDED
#define WSDB_FILE_API_INCLUDED

#define WSDB_BLOCK_SIZE 2048

#include "wsdb_priv.h"

struct file_block {
    uint16_t file_id;
    uint16_t block_id;
};
typedef uint32_t file_addr_t; // replaces file_block

struct file_hdr {
    int magic;
    char name[256];
    uint16_t block_size;
    uint16_t last_block;
};
#define WSDB_FILE_MAGIC 0x50001212

enum file_state {
    FILE_UNITIALIZED = 0,
    FILE_OPEN = 99,
    FILE_CLOSED,
    FILE_USED,
};
enum file_seek_op {
    FILE_POS_FIRST = 0,
    FILE_POS_LAST,
    FILE_POS_EXACT
};

struct wsdb_file {
    char ident;
    struct file_hdr hdr;
    enum file_state state;
    void *info;

    uint16_t (*get_block_size)(struct wsdb_file *);
    int (*seek)(
       struct wsdb_file *file, enum file_seek_op oper, file_addr_t addr
    );
    int (*read_block)(
       struct wsdb_file *file, file_addr_t addr, uint16_t len, void *data
    );
    int (*read_next_block)(
       struct wsdb_file *file, uint16_t len, void *data
    );
    int (*write_block)(
        struct wsdb_file *file, file_addr_t addr, uint16_t len, void *data
    );
    file_addr_t (*append_block)(
        struct wsdb_file *file, uint16_t len, void *data
    );
    int (*close)(struct wsdb_file *file);
};
#define IDENT_wsdb_file 'w'

static inline uint16_t wsdb_file_get_block_size(struct wsdb_file * file) {
    return file->hdr.block_size;
}
#endif
