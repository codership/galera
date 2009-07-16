/**
 * @file writebuf.c
 *
 *
 * @author Teemu Ollakka <teemu.ollakka@codership.com>
 *
 * Copyright (C) 2007 Codership Oy
 */

#include "galeracomm/writebuf.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

writebuf_t *writebuf_new(const void *buf, const size_t s)
{
    writebuf_t *wb;

    wb = malloc(sizeof(writebuf_t));
    wb->refcnt = 1;
    wb->flags = 0;
    wb->hdrlen = 0;
    wb->payload = buf;
    wb->priv_payload = NULL;
    wb->payloadlen = s;
    return wb;
}

writebuf_t *writebuf_copy(const writebuf_t *wb)
{
    writebuf_t *wb_copy;
    wb_copy = malloc(sizeof(writebuf_t));
    *wb_copy = *wb;
    wb_copy->priv_payload = malloc(wb_copy->payloadlen);
    memcpy(wb_copy->priv_payload, wb->payload, wb->payloadlen);
    wb_copy->payload = wb_copy->priv_payload;
    return wb_copy;
}

void writebuf_free(writebuf_t *wb)
{
    if (wb == NULL)
	return;
    if (wb->refcnt == 1) {
	free(wb->priv_payload);
	free(wb);
    } else {
	wb->refcnt--;
    }
}

void writebuf_prepend_hdr(writebuf_t *wb, const void *hdr, const size_t hdrlen)
{
    assert(wb != NULL);
    assert(hdr != NULL && hdrlen != 0);

    if (wb->hdrlen + hdrlen > WRITEBUF_MAX_HDR_LEN) {
	fprintf(stderr, "out of headerspace... either increase "
		"WRITEBUF_MAX_HDR_LEN in writebuf.h or fix your code\n");
	abort();
    }
    memcpy(wb->hdr + WRITEBUF_MAX_HDR_LEN - wb->hdrlen - hdrlen, hdr, hdrlen);
    wb->hdrlen += hdrlen;
}

void writebuf_rollback_hdr(writebuf_t *wb, const size_t len)
{
    assert(wb != NULL);
    assert(wb->hdrlen >= len);
    wb->hdrlen -= len;
}

const void *writebuf_get_hdr(const writebuf_t *wb)
{
    assert(wb != NULL);
    return wb->hdr + WRITEBUF_MAX_HDR_LEN - wb->hdrlen;
}

size_t writebuf_get_hdrlen(const writebuf_t *wb)
{
    assert(wb != NULL);
    return wb->hdrlen;
}

const void *writebuf_get_payload(const writebuf_t *wb)
{
    assert(wb != NULL);
    return wb->priv_payload ? wb->priv_payload : wb->payload;
}

size_t writebuf_get_payloadlen(const writebuf_t *wb)
{
    assert(wb != NULL);
    return wb->payloadlen;
}
