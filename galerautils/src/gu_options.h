// Copyright (C) 2009 Codership Oy <info@codership.com>
 
/**
 * @file
 * A parser for the options string.
 *
 * $Id$
 */

#ifndef _gu_options_h_
#define _gu_options_h_

struct gu_options_token {
    const char* token;
    long        len;
};

struct gu_options_pair {
    struct gu_options_token key;
    struct gu_options_token value;
};

struct gu_options {
    long                   opts_num;
    struct gu_options_pair opts[1];
};

extern struct gu_options*
gu_options_from_string (const char* opts, char pair_sep, char key_sep);

extern char*
gu_options_to_string (const struct gu_options* opts, char pair_sep, char key_sep);

extern void
gu_options_free (struct gu_options* opts);

#endif // _gu_options_h_
