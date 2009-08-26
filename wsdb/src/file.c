// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>

#include "wsdb_priv.h"
#include "file.h"
#include "wsdb_file.h"


struct file_info {
    char ident;
    FILE *fd;
    //uint32_t last_block_id;
};
#define IDENT_file_info 'f'

static int file_read_next_block(
    struct wsdb_file *file, uint16_t len, void *data
) {
    struct file_info *info;
    int rcode;

    CHECK_OBJ(file, wsdb_file);
    info = (struct file_info *)file->info;
    CHECK_OBJ(info, file_info);
    assert(len == file->hdr.block_size);
    
    if ((rcode = fread(data, (size_t)1, len, info->fd)) != len) {
        if (ferror(info->fd)) {
            gu_error ("file read failed: %s", file->hdr.name);
            assert(0);
        }
        return WSDB_ERR_FILE_END;
    }
    return WSDB_OK;
}

static int file_read_block(
    struct wsdb_file *file, file_addr_t block_id, uint16_t len, void *data
) {
    struct file_info *info;
    int rcode;

    CHECK_OBJ(file, wsdb_file);
    info = (struct file_info *)file->info;
    CHECK_OBJ(info, file_info);
    assert(len == file->hdr.block_size);
    
    rcode = fseek(
        info->fd, (size_t)(block_id * file->hdr.block_size), SEEK_SET
    );

    return file_read_next_block(file, len, data);
}

static int file_seek(
    struct wsdb_file *file, enum file_seek_op oper, file_addr_t block_id
) {
    struct file_info *info;
    int rcode;

    CHECK_OBJ(file, wsdb_file);
    info = (struct file_info *)file->info;
    CHECK_OBJ(info, file_info);

    switch (oper) {
    case FILE_POS_FIRST:
        rcode = fseek(info->fd, (size_t)0, SEEK_SET);
        break;
    case FILE_POS_LAST:
        rcode = fseek(info->fd, (size_t)0, SEEK_END);
        break;
    case FILE_POS_EXACT:
        rcode = fseek(
            info->fd, (size_t)(block_id * file->hdr.block_size), SEEK_SET
        );
    }
    return WSDB_OK;
}
static int file_write_block(
    struct wsdb_file *file, file_addr_t block_id, uint16_t len, void *data
) {
    int rcode;
    struct file_info *info = (struct file_info *)file->info;
    char *pad = "###########################################\0";
    const ssize_t pad_len = strlen(pad);

    CHECK_OBJ(file, wsdb_file);
    CHECK_OBJ(info, file_info);

    if (len > file->hdr.block_size) {
        gu_error ("Too long block written, WSDB errno: %d",
                  WSDB_ERR_FILE_WRITE);
        return WSDB_ERR_FILE_WRITE;
    }
    
    rcode = fseek(
        info->fd, (size_t)(block_id * file->hdr.block_size), SEEK_SET
    );
    rcode = fwrite(data, (size_t)len, 1, info->fd);
    if (rcode != 1) {
        gu_error ("failed to write file: %s", file->hdr.name);
    }
    while (len < file->hdr.block_size) {
        ssize_t write_len =
            (file->hdr.block_size - len > pad_len) ? 
            pad_len : file->hdr.block_size - len;  
        rcode = fwrite((void *)pad, 1, write_len, info->fd);
        if (rcode != write_len) {
            gu_error ("failed to write pad in file: %s", file->hdr.name);
            return WSDB_ERROR;
        }
        len += write_len;
    }
    
    fflush(info->fd);
    file->state = FILE_USED;
    return WSDB_OK;
}

static int file_close(struct wsdb_file *file) {
    struct file_info *info;

    CHECK_OBJ(file, wsdb_file);
    info = (struct file_info *)file->info;
    CHECK_OBJ(info, file_info);

    file_write_block(file, 0, sizeof(struct file_hdr), (void *)&(file->hdr));
    fclose(info->fd);
    file->state = FILE_CLOSED;
    return WSDB_OK;
}

static file_addr_t file_append_block(
    struct wsdb_file *file, uint16_t len, void *data
) {
    ++(file->hdr.last_block);
    if (file->hdr.last_block == 0)
	file->hdr.last_block = 1;
    file_write_block(file, file->hdr.last_block, len, data);
    return file->hdr.last_block;
}

static struct wsdb_file *make_wsdb_file(char *file_name) {
    struct wsdb_file *file;
    struct file_info *info;
    MAKE_OBJ(file, wsdb_file);
    MAKE_OBJ(info, file_info);

    file->info            = info;
    file->get_block_size  = wsdb_file_get_block_size;
    file->seek            = file_seek;
    file->read_block      = file_read_block;
    file->read_next_block = file_read_next_block;
    file->write_block     = file_write_block;
    file->append_block    = file_append_block;
    file->close           = file_close;
    return file;
}

struct wsdb_file *file_create(char *file_name, uint16_t block_size) {
    struct wsdb_file *file = make_wsdb_file(file_name);
    struct file_info *info = (struct file_info *)file->info;

    info->fd = fopen(file_name, "w+");
    if (!info->fd) {
        gu_error ("Failed to open wsdb file: %s <%d>", file_name, errno);
        gu_free(info); gu_free(file);
        return NULL;
    }
    file->state = FILE_OPEN;

    /* write file header */
    memset(&file->hdr, 0, sizeof(struct file_hdr));
    file->hdr.magic = WSDB_FILE_MAGIC;
    strncpy(file->hdr.name, file_name, 256);
    file->hdr.block_size = block_size;
    file->hdr.last_block = 0;

    file_write_block(file, 0, sizeof(struct file_hdr), (void *)&(file->hdr));
#ifdef REMOVED
    if ((len = fwrite(
             &file->hdr, sizeof(struct file_hdr), 1, info->fd)) !=1 
        ){
        wsdb_log(WSDB_ERR_FILE_OPEN, "could not write header for file %s", 
                 file->hdr.name
            );
        return NULL;
    }
#endif
    return file;
}

struct wsdb_file *file_open(char *file_name) {
    struct wsdb_file *file = make_wsdb_file(file_name);
    struct file_info *info = (struct file_info *)file->info;
    int rcode;
    int i;
    char *data;

    info->fd    = fopen(file_name, "a+");
    file->state = FILE_OPEN;

    fseek(info->fd, 0, SEEK_SET);
    /* read file header */
    if ((rcode = fread(&file->hdr, sizeof(struct file_hdr), 1, info->fd)) != 1
        ){
        if (ferror(info->fd)) {
            gu_error ("file read failed: %s", file->hdr.name);
            assert(0);
        }
        return NULL;
    }

    /* read and count the blocks */
    data = gu_malloc (file->hdr.block_size);

    i = 1;
    while(file_read_block(file, i, file->hdr.block_size, data) == WSDB_OK) i++;
    file->hdr.last_block = i--;
        
    /* set file pointer back to start */
    fseek(info->fd, 0, SEEK_SET);

    return file;
}
