/**
 * @file writebuf.h
 *
 *
 * @author Teemu Ollakka <teemu.ollakka@codership.com>
 *
 * Copyright (C) 2007 Codership Oy
 */
#ifndef WRITEBUF_H
#define WRITEBUF_H

#include <gcomm/types.h>

#define WRITEBUF_MAX_HDR_LEN 64

typedef enum {
    WRITEBUF_PERSISTENT_PAYLOAD = 0x1
} writebuf_flags_e;

typedef struct writebuf_ {
    volatile int refcnt;
    writebuf_flags_e flags; /* Persistence, what else? */
    char hdr[WRITEBUF_MAX_HDR_LEN];
    size_t hdrlen;
    const void *payload;
    void *priv_payload;
    size_t payloadlen;
} writebuf_t;

writebuf_t *writebuf_new(const void *, const size_t);
writebuf_t *writebuf_copy(const writebuf_t *);
void writebuf_free(writebuf_t *);
void writebuf_prepend_hdr(writebuf_t *, const void *, const size_t);
void writebuf_rollback_hdr(writebuf_t *, const size_t);

const void *writebuf_get_hdr(const writebuf_t *);
size_t writebuf_get_hdrlen(const writebuf_t *);
const void *writebuf_get_payload(const writebuf_t *);
size_t writebuf_get_payloadlen(const writebuf_t *);

static inline size_t writebuf_get_totlen(const writebuf_t *wb)
{
    return writebuf_get_hdrlen(wb) + writebuf_get_payloadlen(wb);
}

#endif /* WRITEBUF_H */
