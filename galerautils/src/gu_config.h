// Copyright (C) 2010 Codership Oy <info@codership.com>
 
/**
 * @file
 * C-interface for configuration management
 *
 * $Id$
 */

#ifndef _gu_config_h_
#define _gu_config_h_

#include <stdint.h>

#ifdef __cplusplus 
extern "C" {
#endif

typedef struct gu_config gu_config_t;

gu_config_t*
gu_config_create (const char* params);

void
gu_config_destroy (gu_config_t* cnf);

/* Getters return 0 on success, 1 when key not found, negative error code
 * in case of other errors (conversion failed and such) */

long
gu_config_get_string (gu_config_t* cnf, const char* key, const char** val);

long
gu_config_get_int64  (gu_config_t* cnf, const char* key, int64_t* val);

long
gu_config_get_double (gu_config_t* cnf, const char* key, double* val);

long
gu_config_get_ptr    (gu_config_t* cnf, const char* key, void** val);

long
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

#ifdef __cplusplus 
}
#endif

#endif /* _gu_config_h_ */

