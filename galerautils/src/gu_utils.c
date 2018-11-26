// Copyright (C) 2010 Codership Oy <info@codership.com>

/**
 * @file Miscellaneous utility functions
 *
 * $Id$
 */

#include "gu_utils.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>

const char*
gu_str2ll (const char* str, long long* ll)
{
    char* ret;
    int   shift = 0;

    long long llret = strtoll (str, &ret, 0);

    switch (ret[0])
    {
    case 't':
    case 'T':
        shift += 10;
        /* fall through */
    case 'g':
    case 'G':
        shift += 10;
        /* fall through */
    case 'm':
    case 'M':
        shift += 10;
        /* fall through */
    case 'k':
    case 'K':
        shift += 10;
        ret++;

        if (llret == ((llret << (shift + 1)) >> (shift + 1))) {
            llret <<= shift;
        }
        else { /* ERANGE */
            if (llret > 0) llret = LLONG_MAX;
            else llret = LLONG_MIN;
            errno = ERANGE;
        }
        /* fall through */
    default:
        *ll = llret;
    }

    return ret;
}

const char*
gu_str2dbl (const char* str, double* dbl)
{
    char* ret;
    *dbl = strtod (str, &ret);
    return ret;
}

const char*
gu_str2bool (const char* str, bool* b)
{
    size_t const len = strlen(str);
    int res = -1; /* no conversion */

    switch (len)
    {
    case 1:
        switch (str[0])
        {
        case '0': case 'N': case 'n': res = 0; break;
        case '1': case 'Y': case 'y': res = 1; break;
        }
        break;
    case 2:
        if      (!strcasecmp(str, "on")) res = 1;
        else if (!strcasecmp(str, "no")) res = 0;
        break;
    case 3:
        if      (!strcasecmp(str, "off")) res = 0;
        else if (!strcasecmp(str, "yes")) res = 1;
        else if (!strcasecmp(str, "yep")) res = 1;
        break;
    case 4:
        if      (!strcasecmp(str, "true")) res = 1;
        else if (!strcasecmp(str, "sure")) res = 1;
        else if (!strcasecmp(str, "none")) res = 0;
        else if (!strcasecmp(str, "nope")) res = 0;
        else if (!strcasecmp(str, "yeah")) res = 1;
        break;
    case 5:
        if (!strcasecmp(str, "false")) res = 0;
        break;
    }

    *b = (res > 0);

    return (res >= 0) ? (str + len) : str;
}

const char*
gu_str2ptr (const char* str, void** ptr)
{
    char* ret;
    *ptr = (void*) (intptr_t)strtoll (str, &ret, 16);
    return ret;
}

