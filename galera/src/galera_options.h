// Copyright (C) 2009 Codership Oy <info@codership.com>

#ifndef __GALERA_OPTIONS_H__
#define __GALERA_OPTIONS_H__

#include <stdbool.h>
#include <wsrep_api.h>

struct galera_options
{
    bool   log_debug;
    bool   persistent_writesets;
    size_t local_cache_size;
    char*  dbug_spec;
    bool   append_queries;
};

extern const struct galera_options galera_defaults;

extern wsrep_status_t
galera_options_from_string (struct galera_options* conf, const char* conf_str);

extern char*
galera_options_to_string   (struct galera_options* conf);

#endif // __GALERA_OPTIONS_H__
