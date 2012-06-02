// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file Functions to dump buffer contents in a readable form
 *
 * $Id$
 */

#include "gu_print_buf.h"

#define GU_ASCII_0  0x30
#define GU_ASCII_10 0x3a
#define GU_ASCII_A  0x41
#define GU_ASCII_a  0x61
#define GU_ASCII_A_10 (GU_ASCII_A - GU_ASCII_10)
#define GU_ASCII_a_10 (GU_ASCII_a - GU_ASCII_10)

static inline int
_hex_code (uint8_t const x)
{
    return (x + GU_ASCII_0 + (x > 9)*GU_ASCII_A_10);
}

static inline void
_write_byte_binary (char* const str, uint8_t const byte)
{
    str[0] = _hex_code(byte >> 4);
    str[1] = _hex_code(byte & 0x0f);
}

#define GU_ASCII_ALPHA_START 0x20 /* ' ' */
#define GU_ASCII_ALPHA_END   0x7e /* '~' */

static inline void
_write_byte_alpha (char* const str, uint8_t const byte)
{
    if (byte >= GU_ASCII_ALPHA_START && byte <= GU_ASCII_ALPHA_END)
    {
        str[0] = (char)byte;
        str[1] = '.';
    }
    else
    {
        _write_byte_binary (str, byte);
    }
}

/*! Dumps contents of the binary buffer into a readable form */
void
gu_print_buf(const void* buf, ssize_t const buf_size,
             char* str, ssize_t str_size, bool alpha)
{
    uint8_t* b = (uint8_t*)buf;
    ssize_t i;

    str_size--; /* reserve a space for \0 */

    for (i = 0; i < buf_size && str_size > 1;)
    {
        if (alpha)
            _write_byte_alpha  (str, b[i]);
        else
            _write_byte_binary (str, b[i]);

        str      += 2;
        str_size -= 2;
        i++;

        if (0 == (i % 4) && str_size > 0 && i < buf_size)
        {
            /* insert space after every 4 bytes and newline after every 32 */
            str[0] = (i % 32) ? ' ' : '\n';
            str_size--;
            str++;
        }
    }

    str[0] = '\0';
}

