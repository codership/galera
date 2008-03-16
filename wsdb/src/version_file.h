// Copyright (C) 2007 Codership Oy <info@codership.com>

#ifndef VERSION_FILE_INCLUDED
#define VERSION_FILE_INCLUDED

#include "wsdb_priv.h"
#include "wsdb_file.h"

/*
 * @brief removes all version files from given firectory with given basename
 * @param directory directory where to look for version files
 * @param base_name base name of version files
 */
int version_file_remove(const char *directory, const char *base_name);

struct wsdb_file *version_file_open (const char *directory,
				     const char *base_name,
				     uint16_t block_size,
				     uint16_t file_size);

#endif
