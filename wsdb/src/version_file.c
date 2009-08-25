// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

#include "wsdb_file.h"
#include "file.h"
#include "version_file.h"
#include "wsdb_priv.h"

struct version_file {
    char              ident;
    char              directory[256];
    char              base_name[256];
    uint16_t          last_version;
    struct wsdb_file *file;
    uint16_t          block_size;     //!<< block size in bytes
    uint16_t          block_pos;      //!<< next write position in block
    void             *block;          //!<< open data block
    uint16_t          file_size;      //!<< max number of blocks in file
    uint16_t          block_id;       //!<< count of current block
    uint16_t          current_version;
};
#define IDENT_version_file 'v'

static void make_version_file_name(
    char *name, uint16_t len, struct version_file *vf
) {
    memset(name, 0, len);
    snprintf(name, len, "%s%s%s%d", 
             vf->directory, PATH_SEPARATOR, vf->base_name, vf->current_version
    );
}
static void remove_version_files(struct version_file *vf) {
    struct dirent *dp;
    DIR *dir = opendir(vf->directory);
    if (!dir) {
        mkdir(vf->directory, 0x666);
        return;
    }
    while ((dp = readdir(dir))) {
        if (!strncmp(dp->d_name, vf->base_name, strlen(vf->base_name))) {
            char path[1024];
            memset(path, 0, 1024);
            snprintf(path, 1024, "%s%s%s",
                     vf->directory, PATH_SEPARATOR, dp->d_name
            );
            
            if(remove(path)) {
                gu_error("Failed to delete file: %s", path);
            }
        }
    }
    closedir(dir);
}

/* not used
static struct wsdb_file *open_file(
    struct version_file *vf, uint16_t block_size
) {
    struct wsdb_file *file;
// unused    struct dirent *dp;
    uint16_t max_version = 0;
    char file_name[256];
    
    vf->current_version = vf->last_version = max_version;
    make_version_file_name(file_name, 256, vf);
    file = file_open(file_name);
    if (!file) {
        return NULL;
    } else {
        if (file->hdr.block_size != block_size) {
            wsdb_log(WSDB_ERR_FILE_OPEN, "bad file block size detected");
            return NULL;
        }
    }
    return file;
}
*/

static int scan_versions(
    struct version_file *vf, int *min_version, int *max_version
) {
    struct dirent *dp;
    *max_version = *min_version = -1;
    
    DIR *dir = opendir(vf->directory);
    if (!dir) {
        gu_warn("no version files found");
        return WSDB_ERR_FILE_NOTFOUND;
    }
    while ((dp = readdir(dir))) {
        if (!strncmp(dp->d_name, vf->base_name, strlen(vf->base_name))) {
            char version_str[256] = {0};
            int version;
            strcpy(version_str, dp->d_name + strlen(vf->base_name));
            version = atoi(version_str);
            if (version > *max_version || *max_version == -1) {
                *max_version = version;
            }
            if (version < *min_version || *min_version == -1) {
                *min_version = version;
            }
        }
    }
    
    closedir(dir);
    return WSDB_OK;
}

static struct wsdb_file *open_first_file(
    struct version_file *vf, uint16_t block_size
) {
    struct wsdb_file *file;
    int min_version, max_version = 0;
    char file_name[256];
    
    if (scan_versions(vf, &min_version, &max_version)) return NULL;

    vf->current_version = (uint16_t)min_version;
    vf->last_version    = (uint16_t)max_version;
    make_version_file_name(file_name, 256, vf);
    file = file_open(file_name);
    if (!file) {
        gu_warn("version file does not exist");
        return NULL;
    } else {
        if (file->hdr.block_size != block_size) {
            gu_error("bad file block size detected");
            return NULL;
        }
    }
    return file;
}
static struct wsdb_file *open_last_file(
    struct version_file *vf, uint16_t block_size
) {
    struct wsdb_file *file;
    struct dirent *dp;
    uint16_t max_version = 0;
    char file_name[256];
    
    DIR *dir = opendir(vf->directory);
    if (!dir) {
        gu_warn("no version files found");
        return NULL;
    }
    while ((dp = readdir(dir))) {
        if (!strncmp(dp->d_name, vf->base_name, strlen(vf->base_name))) {
            char version_str[256] = {0};
            int version;
            strcpy(version_str, dp->d_name + strlen(vf->base_name));
            version = atoi(version_str);
            if (version > max_version) {
                max_version = version;
            }
        }
    }
    
    closedir(dir);
    vf->current_version = vf->last_version = max_version;
    make_version_file_name(file_name, 256, vf);
    file = file_open(file_name);
    if (!file) {
        file = file_create(file_name, block_size);
    } else {
        if (file->hdr.block_size != block_size) {
            gu_error("bad file block size detected");
            return NULL;
        }
    }
    return file;
}

static uint16_t accumulate_file(struct version_file *vf) {
    char file_name[256];
    vf->file->close(vf->file);
    vf->current_version = ++(vf->last_version);

    make_version_file_name(file_name, 256, vf);
    vf->file = file_create(file_name, vf->block_size);
    vf->block_id = 0;
    return vf->last_version;
}

static void accumulate_block(struct version_file *vf) {
    if (vf->block_id > vf->file_size) {
        vf->current_version = accumulate_file(vf);
    }
    vf->file->append_block(vf->file, vf->block_size, vf->block);
    memset(vf->block, vf->block_size, 0);
    vf->block_pos = 0;
    vf->block_id++;
}

static file_addr_t put_in_block(
    struct version_file *vf, uint16_t len, void *data
) {
    file_addr_t addr = vf->current_version;
    addr = addr<<16;
    addr += vf->file->hdr.last_block;

    while (len) {
        size_t stored = (len > vf->block_size - vf->block_pos) ? 
            vf->block_size - vf->block_pos : len;
        memcpy((char*)vf->block + vf->block_pos, data, stored);
        vf->block_pos += stored;
        len  -= stored;
        data = (char*)data + stored;
        if (len) accumulate_block(vf);
    }
    return addr;
}

static file_addr_t version_file_append_block(
    struct wsdb_file *file, uint16_t len, void *data
) {
    struct version_file *info;
    CHECK_OBJ(file, wsdb_file);

    info = (struct version_file *)file->info;
    CHECK_OBJ(info, version_file);

    file_addr_t addr = put_in_block(info, len, data);
    return addr;
}

static int version_file_seek(
    struct wsdb_file *file, enum file_seek_op oper, file_addr_t addr
) {
    struct version_file *vf = (struct version_file *)file->info;

    switch (oper) {
    case FILE_POS_EXACT:
      break;
    case FILE_POS_FIRST:
      vf->file = open_first_file(vf, vf->block_size);
      break;
    case FILE_POS_LAST:
      vf->file = open_last_file(vf, vf->block_size);
      break;

    }
    if (vf->file) {
        return WSDB_OK;
    } else {
        return WSDB_ERR_FILE_NOTFOUND;
    }
}

static int version_file_read_next_block(
    struct wsdb_file *file, uint16_t len, void *data
) {
    struct version_file *vf = (struct version_file *)file->info;
    /* read current block from block file */
    int rcode = vf->file->read_next_block(vf->file, len, data);
    if (rcode == WSDB_ERR_FILE_END) {
        char file_name[256];
        if (vf->current_version == vf->last_version) {
            /* reached last versioned file */
            return WSDB_ERR_FILE_END;
        }
        /* step to next version file */
        vf->current_version++;
        make_version_file_name(file_name, 256, vf);
        vf->file = file_open(file_name);

        /* try to read, file may be empty also */
        rcode = vf->file->read_next_block(vf->file, len, data);
        if (rcode == WSDB_ERR_FILE_END) {
            return WSDB_ERR_FILE_END;
        }
    }

    return WSDB_OK;
}

static int version_file_read_block(
    struct wsdb_file *file, file_addr_t addr, uint16_t len, void *data
) {
    //struct version_file *info = (struct version_file *)file->info;
    return WSDB_OK;
}
static int version_file_write_block(
    struct wsdb_file *file, file_addr_t addr, uint16_t len, void *data
) {
    //struct version_file *info = (struct version_file *)file->info;
    return WSDB_OK;
}
static int version_file_close(struct wsdb_file *file) {
    //struct version_file *info = (struct version_file *)file->info;
    return WSDB_OK;
}

static uint16_t get_block_size(struct wsdb_file *file) {
    struct version_file *vf = (struct version_file *)file->info;
    if (vf->file) {
        return  vf->file->get_block_size(vf->file);
    } else {
        return wsdb_file_get_block_size(file);
    }
}

static struct wsdb_file *create_wsdb_file() {
    struct wsdb_file *file;
    struct version_file *info;
    MAKE_OBJ(file, wsdb_file);
    MAKE_OBJ(info, version_file);

    file->info            = info;
    file->get_block_size  = get_block_size;
    file->seek            = version_file_seek;
    file->read_next_block = version_file_read_next_block;
    file->read_block      = version_file_read_block;
    file->write_block     = version_file_write_block;
    file->append_block    = version_file_append_block;
    file->close           = version_file_close;
    return file;
}

int version_file_remove (
    const char *directory, const char *base_name
) {
    struct wsdb_file *file = create_wsdb_file();
    struct version_file *vf = (struct version_file*)file->info;

    strncpy(vf->directory, directory, 256);
    strncpy(vf->base_name, base_name, 256);
    
    remove_version_files(vf);
    return WSDB_OK;
}

struct wsdb_file *version_file_open (const char *directory,
				     const char *base_name,
				     uint16_t block_size,
				     uint16_t file_size)
{
    struct wsdb_file *file = create_wsdb_file();
    struct version_file *vf = (struct version_file*)file->info;

    strncpy(vf->directory, directory, 256);
    strncpy(vf->base_name, base_name, 256);
    
    vf->file = open_last_file(vf, block_size);
    if (!vf->file) {
        gu_error("Failed to open version file");
        return NULL;
    }
    if (vf->file->get_block_size(vf->file) != block_size){
        gu_error("incompatible file block size");
        return NULL;
    }

    vf->block_size = block_size;
    vf->block = gu_malloc(block_size);
    memset(vf->block, vf->file->get_block_size(vf->file), 0);
    
    vf->block_pos = 0;
    vf->file_size = file_size;
    vf->block_id = 0;    
    file->state = FILE_OPEN;
    return file;
}

file_addr_t version_file_get_first_addr(struct wsdb_file *file)
{
// unused    struct version_file *vf = (struct version_file*)file->info;

    file_addr_t addr = 0;
    return addr;
}
file_addr_t version_file_addr_next(struct wsdb_file *file, file_addr_t addr) {
    struct version_file *vf = (struct version_file*)file->info;

    uint16_t version  = addr>>16;
    uint16_t block_id = addr & 0xFFFF;

    if (++block_id == vf->file_size) {
        version++;
        block_id = 0;
    }
    addr = version;
    addr = addr<<16;
    addr += block_id;
    return addr;
}

