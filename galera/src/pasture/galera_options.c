// Copyright (C) 2009 Codership Oy <info@codership.com>

/* Realistically we want to rewrite it in C++ and make each option an object
 * with getter and setter methods, etc. and store them in a std::map */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <galerautils.h>

#include "galera_options.h"

#define OPT_SEP ';' // separates option pairs
#define KEY_SEP '=' // separates key and value

#define LOG_DEBUG_KEY        "log_debug"
#define PERSISTENT_WS_KEY    "persistent_writesets"
#define LOCAL_CACHE_SIZE_KEY "local_cache_size"
#define DBUG_SPEC_KEY        "dbug_spec"

const struct galera_options galera_defaults = { 
    false,
    false,
    20971520, /* 20Mb */ // should be WSDB_LOCAL_CACHE_SIZE
    NULL
};

// NOTE: it is guaranteed that var starts with alnum character.
static bool
strtobool (const char* str)
{
    if (
        !strcasecmp(str, "no")  ||
        !strcasecmp(str, "off") ||
        !strcasecmp(str, "n")   ||
        ('0' == *str && 0 == strtol (str, NULL, 36))
        )
        return false;
    else
        return true;
}

static void
options_set_log_debug (struct galera_options* opts, const char* val)
{
    if (val) {
        bool bval = strtobool (val);
        if (opts->log_debug != bval) {
            opts->log_debug = bval;
            if (bval) {
                gu_conf_debug_on();
            }
            else {
                gu_conf_debug_off();
            }
        }
        return;
    }

    gu_warn ("Missing value for %s option. Skipping", LOG_DEBUG_KEY);
}

static void
options_set_ws_persistency (struct galera_options* opts, const char* val)
{
    if (val) {
        opts->persistent_writesets = strtobool (val);
        return;
    }
    
    gu_warn ("Missing value for %s option. Skipping", PERSISTENT_WS_KEY);
}

static void
options_set_cache_size (struct galera_options* opts, const char* val)
{
    if (val) {
        char* end;
        long long ival = strtoll (val, &end, 0);
        if (*end == '\0') {
            opts->local_cache_size = ival;
            return;
        }
    }

    gu_warn ("Bad value for %s option: %s. Skipping.",
             LOCAL_CACHE_SIZE_KEY, val ? val : "(null)");
}

static void
options_set_dbug_spec (struct galera_options* opts, const char* val)
{
    if (opts->dbug_spec) free (opts->dbug_spec);

    if (val) {
        opts->dbug_spec = strdup (val);
        GU_DBUG_PUSH(val);
    }
    else {
        opts->dbug_spec = NULL;
        GU_DBUG_POP();
    }
}

wsrep_status_t
galera_options_from_string (struct galera_options* opts, const char* opts_str)
{
    struct gu_options* o = gu_options_from_string (opts_str, OPT_SEP, KEY_SEP);

    if (o) {
        long i;

        for (i = 0; i < o->opts_num; i++) {

            const char* key = o->opts[i].key.token;
            const char* val = o->opts[i].value.token;

            assert (key);

            if      (!strcmp (key, LOG_DEBUG_KEY)) {
                options_set_log_debug (opts, val);
            }
            else if (!strcmp (key, PERSISTENT_WS_KEY)) {
                options_set_ws_persistency (opts, val);
            }
            else if (!strcmp (key, LOCAL_CACHE_SIZE_KEY)) {
                options_set_cache_size (opts, val);
            }
            else if (!strcmp (key, DBUG_SPEC_KEY)) {
                options_set_dbug_spec (opts, val);
            }
            else {
                gu_warn ("Unrecognized option key: '%s', skipping.", key);
            }
        }

        gu_options_free (o);
        return WSREP_OK;
    }
    else {
        gu_warn ("Failed to parse the options string. No options changed.");
        return WSREP_WARNING;
    }
}

char*
galera_options_to_string (struct galera_options* opts)
{
    const size_t max_opts_len = 2047; // should be carefully adjusted
    char  tmp[max_opts_len + 1];

    size_t opts_len = snprintf (
        tmp, max_opts_len,
        "%s %c %d%c\n" "%s %c %d%c\n" "%s %c %zu%c\n" "%s %c %s",
        LOG_DEBUG_KEY, KEY_SEP, (opts->log_debug != 0), OPT_SEP,
        PERSISTENT_WS_KEY, KEY_SEP, (opts->persistent_writesets != 0), OPT_SEP,
        LOCAL_CACHE_SIZE_KEY, KEY_SEP, opts->local_cache_size, OPT_SEP,
        DBUG_SPEC_KEY, KEY_SEP, opts->dbug_spec ? opts->dbug_spec : ""
        );
    if (opts_len >= max_opts_len) return NULL;

    return strdup (tmp);
}
