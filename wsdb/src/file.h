// Copyright (C) 2007 Codership Oy <info@codership.com>

#ifndef WSDB_FILE_INCLUDED
#define WSDB_FILE_INCLUDED

#include "wsdb_priv.h"
#include "wsdb_file.h"

struct wsdb_file *file_open(char *file_name);
struct wsdb_file *file_create(char *file_name, uint16_t block_size);

#endif
