/**
 * @file readbuf.c
 *
 *
 * @author Teemu Ollakka <teemu.ollakka@codership.com>
 *
 * Copyright (C) 2007 Codership Oy
 */

#include "gcomm/readbuf.h"

#include <assert.h>
#include <stdlib.h>

readbuf_t *readbuf_new(const void *buf, const size_t buflen)
{
    readbuf_t *rb;
    
    assert(buf != NULL && buflen != 0);
    
    rb = malloc(sizeof(readbuf_t));
    rb->refcnt = 1;
    rb->buf = buf;
    rb->priv_buf = NULL;
    rb->buflen = buflen;
    return rb;
}

readbuf_t *readbuf_copy(const readbuf_t *rb)
{
    assert(rb);
    if (rb->priv_buf == NULL) {
	((readbuf_t *) rb)->priv_buf = malloc(rb->buflen);
	memcpy(((readbuf_t *) rb)->priv_buf, rb->buf, rb->buflen);
    }
    ((readbuf_t *) rb)->refcnt++;
    return (readbuf_t *) rb;
}

void readbuf_free(readbuf_t *rb)
{
    if (rb == NULL)
	return;
    if (rb->refcnt == 1) {
	free(rb->priv_buf);
	free(rb);
    } else {
	rb->refcnt--;
    }
}

const void *readbuf_get_buf(const readbuf_t *rb, const size_t offset)
{
    const char *ret_base;
    assert(rb);
    if (rb->priv_buf != NULL)
	ret_base = rb->priv_buf;
    else 
	ret_base = rb->buf;
    return ret_base == NULL ? NULL : ret_base + offset;
}

size_t readbuf_get_buflen(const readbuf_t *rb)
{
    assert(rb);
    return rb->buflen;
}
