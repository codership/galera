/**
 * @file readbuf.h
 *
 *
 * @author Teemu Ollakka <teemu.ollakka@codership.com>
 *
 * Copyright (C) 2007 Codership Oy
 */
#ifndef READBUF_H
#define READBUF_H

#include <gcomm/types.h>

/**
 * Read buffer is passed upwards to the protocol stack.
 */
typedef struct readbuf_ {
    volatile int refcnt;
    const void *buf;
    void *priv_buf;
    size_t buflen;
} readbuf_t;

readbuf_t *readbuf_new(const void *buf, const size_t buflen);
readbuf_t *readbuf_copy(const readbuf_t *rb);
void readbuf_free(readbuf_t *rb);
const void *readbuf_get_buf(const readbuf_t *rb, const size_t offset);
size_t readbuf_get_buflen(const readbuf_t *rb);

#endif /* READBUF_H */
