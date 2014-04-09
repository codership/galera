// Copyright (C) 2010-2014 Codership Oy <info@codership.com>

/**
 * @file
 * C-interface for configuration management
 *
 * $Id$
 */

#ifndef _gu_config_h_
#define _gu_config_h_

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h> // for ssize_t

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gu_config gu_config_t;

gu_config_t*
gu_config_create (void);

void
gu_config_destroy (gu_config_t* cnf);

bool
gu_config_has (gu_config_t* cnf, const char* key);

bool
gu_config_is_set (gu_config_t* cnf, const char* key);

/* before setting a parameter, it must be added to a known parameter list*/
int
gu_config_add (gu_config_t* cnf, const char* key,
               const char* val /*can be NULL*/);

/* Getters/setters return 0 on success, 1 when key not set/not found,
 * negative error code in case of other errors (conversion failed and such) */

int
gu_config_get_string (gu_config_t* cnf, const char* key, const char** val);

int
gu_config_get_int64  (gu_config_t* cnf, const char* key, int64_t* val);

int
gu_config_get_double (gu_config_t* cnf, const char* key, double* val);

int
gu_config_get_ptr    (gu_config_t* cnf, const char* key, void** val);

int
gu_config_get_bool   (gu_config_t* cnf, const char* key, bool* val);

void
gu_config_set_string (gu_config_t* cnf, const char* key, const char* val);

void
gu_config_set_int64  (gu_config_t* cnf, const char* key, int64_t val);

void
gu_config_set_double (gu_config_t* cnf, const char* key, double val);

void
gu_config_set_ptr    (gu_config_t* cnf, const char* key, const void* val);

void
gu_config_set_bool   (gu_config_t* cnf, const char* key, bool val);

ssize_t
gu_config_print      (gu_config_t* cnf, char* buf, ssize_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* _gu_config_h_ */

