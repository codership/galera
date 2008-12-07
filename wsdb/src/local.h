// Copyright (C) 2007 Codership Oy <info@codership.com>

/*!
 * @file conn.h
 * @brief keyed variable length array utility API 
 * 
 */
#ifndef LOCAL_H_INCLUDED
#define LOCAL_H_INCLUDED

int local_open(const char *dir, const char *file,
	       uint16_t block_size, uint16_t trx_limit);
void local_close();


#endif
