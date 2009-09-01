// Copyright (C) 2009 Codership Oy <info@codership.com>
 
/**
 * @file
 * A parser for the options string.
 *
 * $Id$
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "gu_assert.h"
#include "gu_log.h"
#include "gu_mem.h"
#include "gu_options.h"

static char*
options_strndup (const char* src, size_t len)
{
    char* ret = gu_malloc (len + 1);

    if (ret) {

        strncpy (ret, src, len);
        ret [len] = '\0';
    }

    return ret;
}

static struct gu_options*
options_push_pair (struct gu_options* opts,
                   const char* key_start, const char* key_end,
                   const char* val_start, const char* val_end)
{
    if (key_start)   // ignore null key
    {
        long ret_size = sizeof(*opts) + 
            (opts->opts_num + 1) * sizeof(struct gu_options_pair);

        struct gu_options* ret = gu_realloc (opts, ret_size);

        if (ret) {

            struct gu_options_pair* p = &ret->opts[ret->opts_num];

            opts = ret;
            opts->opts_num++;

            assert (key_end >= key_start);
            assert (val_end || !val_start);

            memset (p, 0, sizeof(*p));

            p->key.len   = key_end - key_start + 1;
            p->key.token = options_strndup (key_start, p->key.len);
            if (!p->key.token) goto enomem;

            if (val_start) {

                assert (val_end >= val_start);
                p->value.len   = val_end - val_start + 1;
                p->value.token = options_strndup (val_start, p->value.len);
                if (!p->value.token) goto enomem;
            }
        }
    }

    return opts;

enomem:
    gu_error ("Out of memory when parsing options");
    gu_options_free (opts);
    return NULL;
}

enum { BEFORE_KEY, IN_KEY, BEFORE_VALUE, IN_VALUE };

struct gu_options*
gu_options_from_string (const char* opts, char pair_sep, char key_sep)
{
    long               ret_size  = sizeof (struct gu_options);
    struct gu_options* ret       = gu_calloc (1, ret_size);
    const char*        scan_pos  = opts;
    const char*        key_start = NULL;
    const char*        key_end   = NULL;
    const char*        val_start = NULL;
    const char*        val_end   = NULL;
    int                state = BEFORE_KEY;

    if (ret) {

        while ('\0' != *scan_pos) {

            if (*scan_pos == pair_sep) {

                ret = options_push_pair (ret,
                                         key_start, key_end,
                                         val_start, val_end);

                if (!ret) return NULL;

                key_start = key_end = val_start = val_end = NULL;
                state = BEFORE_KEY;
            }
            else if (*scan_pos == key_sep) {

                switch (state) {
                case IN_KEY :  // key section ends
                    state = BEFORE_VALUE;
                    break;
                case IN_VALUE: // treat it as a regular character?
                    val_end = scan_pos;
                    break;
                default:
                    gu_error ("Error parsing options string: "
                              "unexpected '%c' at position %zd in '%s'",
                              key_sep, scan_pos - opts, opts);
                    gu_options_free (ret);
                    ret = NULL;
                    goto out;
                }
            }
            else {

                if (isalnum(*scan_pos)) {
                    switch (state) {
                    case BEFORE_KEY:
                        state     = IN_KEY;
                        key_start = scan_pos;
                    case IN_KEY:
                        key_end   = scan_pos;
                        break;
                    case BEFORE_VALUE:
                        state     = IN_VALUE;
                        val_start = scan_pos;
                    case IN_VALUE:
                        val_end   = scan_pos;
                        break;
                    default:
                        assert (0);
                    }
                }
            }

            scan_pos++;
        }

        // last pair may be unterminated
        ret = options_push_pair (ret, key_start, key_end, val_start, val_end);
    }

out:
    return ret;
}

char*
gu_options_to_string (const struct gu_options* opts,
                      char                     pair_sep,
                      char                     key_sep)
{
    long  i;
    long  ret_len = 0;
    char* ret;

    for (i = 0; i < opts->opts_num; i++) {

        assert (opts->opts[i].key.token);
        ret_len += opts->opts[i].key.len;
        ret_len += opts->opts[i].value.len;
        ret_len += 6; // blanks and separators
    }

    ret_len += 1; // termination

    ret = calloc (ret_len, sizeof(char));

    if (ret) {

        char* pos = ret;

        for (i = 0; i < opts->opts_num; i++) {

            pos += snprintf (pos, ret_len - (pos - ret),
                             "%s %c %s %c ",
                             opts->opts[i].key.token, key_sep,
                             opts->opts[i].value.token ?
                             opts->opts[i].value.token : "",
                             pair_sep);
        }
    }

    return ret;
}

static inline void
options_free_token (struct gu_options_token* t)
{
    if (t->token)
    {
        assert (t->len);
        gu_free ((char*)t->token);
    }
}

static void
options_free_pair (struct gu_options_pair* p)
{
    options_free_token (&p->key);
    options_free_token (&p->value);
}

void
gu_options_free (struct gu_options* opts)
{
    long i;

    for (i = 0; i < opts->opts_num; i++) options_free_pair (&opts->opts[i]);

    gu_free (opts);
}

