/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#ifndef GU_STR_H
#define GU_STR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/*!
 * Append after position
 */
static inline char* gu_str_append(char* str, size_t* off, 
                    const char* app, size_t app_len)
{
    char* tmp;
    assert(str == NULL || *(str + *off - 1) == '\0');
    tmp = realloc(str, *off + app_len + 1);
    if (tmp != NULL)
    {
        memcpy(tmp + *off, app, app_len + 1);
        *off += app_len + 1;
    }
    return tmp;
}

/*!
 * Get next string after position
 */
static inline const char* gu_str_next(const char* str)
{
    return strchr(str, '\0') + 1;
}

/*!
 * Advance position starting from over n
 */
static inline const char* gu_str_advance(const char* str, size_t n)
{
    const char* ptr = str;
    while (n-- > 0)
    {
        ptr = gu_str_next(ptr);
    }
    return ptr;
}


/*
 * Utilities to construct and scan tables from null terminated strings.
 * The table format is the following:
 *
 * name\0\columns\0\rows\0
 * colname0\0colname1\0...
 * elem00\0elem01\0elem02\0...
 * elem10\0elem11\0elem\12\...
 * .
 * .
 * .
 */


static inline char* gu_str_table_set_name(char* str, size_t* off, const char* name)
{
    return gu_str_append(str, off, name, strlen(name));
}

static inline const char* gu_str_table_get_name(const char* str)
{
    return str;
}

static inline char* gu_str_table_append_size(char* str, size_t* off, size_t n)
{
    char buf[10];
    size_t len = snprintf(buf, sizeof(buf), "%zu", n);
    return gu_str_append(str, off, buf, len);
}

static inline char* gu_str_table_set_n_cols(char* str, size_t* off, size_t n)
{
    return gu_str_table_append_size(str, off, n);
}

static inline size_t gu_str_table_get_n_cols(const char* str)
{
    str = gu_str_advance(str, 1);
    return strtoul(str, NULL, 0);
}

static inline char* gu_str_table_set_n_rows(char* str, size_t* off, size_t n)
{
    return gu_str_table_append_size(str, off, n);
}

static inline size_t gu_str_table_get_n_rows(const char* str)
{
    str = gu_str_advance(str, 2);
    return strtoul(str, NULL, 0);
}


static inline char* gu_str_table_set_cols(char* str, 
                                                size_t *off,
                                                size_t n, 
                                                const char* cols[])
{
    size_t i;
    for (i = 0; i < n; ++i)
    {
        str = gu_str_append(str, off, cols[i], strlen(cols[i]));
    }
    return str;
}

static inline char* gu_str_table_append_row(char* str, 
                                            size_t *off,
                                            size_t n, 
                                            const char* row[])
{
    size_t i;
    for (i = 0; i < n; ++i)
    {
        str = gu_str_append(str, off, row[i], strlen(row[i]));
    }
    return str;
}


static inline const char* gu_str_table_get_cols(const char* str, size_t n,
    char const* row[])
{
    size_t i;
    str = gu_str_advance(str, 3);
    for (i = 0; i < n; i++)
    {
        row[i] = str;
        str = gu_str_next(str);
    }
    return str;
}


static inline const char* gu_str_table_rows_begin(const char* str, size_t n)
{
    return gu_str_advance(str, 3 + n);
}

static inline const char* gu_str_table_row_get(const char* str, 
                                        size_t n, 
                                        char const* row[])
{
    size_t i;
    for (i = 0; i < n; ++i)
    {
        row[i] = str;
        str = gu_str_next(str);
    }
    return str;
}

static inline void gu_str_table_print_row(FILE* file, size_t n, 
                                          const char* const row[])
{
    size_t i;
    for (i = 0; i < n; ++i)
    {
        fprintf(file, "%s ", row[i]);
    }
    fprintf(file, "\n");
}

static inline void gu_str_table_print(FILE* file, const char* str)
{
    size_t i;
    size_t n_cols, n_rows;
    const char* ptr;
    char const**vec;
    fprintf(file, "%s\n", gu_str_table_get_name(str));
    n_cols = gu_str_table_get_n_cols(str);
    n_rows = gu_str_table_get_n_rows(str);
    
    vec = malloc(n_cols*sizeof(char*));
    ptr = gu_str_table_get_cols(str, n_cols, vec);
    gu_str_table_print_row(file, n_cols, vec);
    for (i = 0; i < n_rows; ++i)
    {
        ptr = gu_str_table_row_get(ptr, n_cols, vec);
        gu_str_table_print_row(file, n_cols, vec);
    }
    free(vec);
}


#endif /* GU_STR_H */
