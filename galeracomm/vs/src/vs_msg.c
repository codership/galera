#include "galeracomm/vs_msg.h"
#include <glib.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

/*
 * version     2 bytes
 * type        2 bytes
 * source      4 bytes
 * source_view 4 bytes
 * seq         8 bytes
 * safety      4 bytes
 * total       24 bytes (ouch)
 */
#define VS_MSG_HDR_LEN 24

struct vs_msg_ {
    volatile int refcnt;
    int version;
    vs_msg_e type;
    addr_t source;
    vs_view_id_t source_view;
    seq_t seq;
    vs_msg_safety_e safety;
    char hdr[VS_MSG_HDR_LEN];
    size_t hdrlen;
    size_t read_offset;
    const readbuf_t *read_msgbuf;
    readbuf_t *priv_read_msgbuf;
};

/*******************************************************************
 * Message
 *******************************************************************/

static size_t vs_msg_write_hdr(vs_msg_t *msg)
{
    size_t offset;
    
    if (!(offset = write_uint16(msg->version, msg->hdr, msg->hdrlen, 0)))
	goto err;
    if (!(offset = write_uint16(msg->type, msg->hdr, msg->hdrlen, offset)))
	goto err;
    if (!(offset = write_uint32(msg->source, msg->hdr, msg->hdrlen, offset)))
	goto err;
    if (!(offset = write_uint32(msg->source_view, msg->hdr, msg->hdrlen, offset)))
	goto err;
    if (!(offset = write_uint64(msg->seq, msg->hdr, msg->hdrlen, offset)))
	goto err;
    if (!(offset = write_uint32(msg->safety, msg->hdr, msg->hdrlen, offset)))
	goto err;     
    return offset;
err:
    return 0;
}

const void *vs_msg_get_hdr(const vs_msg_t *msg)
{
    assert(msg != NULL);
    return msg->hdr;
}

size_t vs_msg_get_hdrlen(const vs_msg_t *msg)
{
    assert(msg != NULL);
    return msg->hdrlen;
}

vs_msg_t *vs_msg_new(const vs_msg_e type, 
		     const addr_t source,
		     const vs_view_id_t source_view,
		     const seq_t seq, 
		     const vs_msg_safety_e safety)
{
     vs_msg_t *msg;
     
     msg = malloc(sizeof(vs_msg_t));
     msg->refcnt = 1;
     msg->version = 1;
     msg->type = type;
     msg->source = source;
     msg->source_view = source_view;
     msg->seq = seq;
     msg->safety = safety;
     msg->read_offset = 0;
     msg->read_msgbuf = NULL;
     msg->priv_read_msgbuf = NULL;
     msg->hdrlen = VS_MSG_HDR_LEN;

     if (!vs_msg_write_hdr(msg)) {
	 free(msg);
	 msg = NULL;
     }
     
     return msg;
}

vs_msg_t *vs_msg_copy(const vs_msg_t *msg)
{
    vs_msg_t *ncmsg = (vs_msg_t *) msg;
    if (g_atomic_int_compare_and_exchange(&ncmsg->refcnt, 1, 2)) {
	if (ncmsg->read_msgbuf && !ncmsg->priv_read_msgbuf) {
	    ncmsg->priv_read_msgbuf = readbuf_copy(ncmsg->read_msgbuf);
	    ncmsg->read_msgbuf = ncmsg->priv_read_msgbuf;
	}
    } else {
	g_atomic_int_add(&ncmsg->refcnt, 1);
    }
    return ncmsg;
}


void vs_msg_free(vs_msg_t *msg)
{
    if (!msg)
	return;
    if (g_atomic_int_compare_and_exchange(&msg->refcnt, 1, 0)) {
	readbuf_free(msg->priv_read_msgbuf);
	free(msg);
    } else {
	g_atomic_int_add(&msg->refcnt, -1);
    }
}

vs_msg_t *vs_msg_read(const readbuf_t *msg, const size_t roff)
{
    vs_msg_t *ret;
    size_t offset;
    uint16_t version;
    uint16_t type;
    addr_t source;
    vs_view_id_t source_view;
    seq_t seq;
    vs_msg_safety_e safety;
    const void *payload;
    size_t payload_len;
     
    payload = readbuf_get_buf(msg, roff);
    payload_len = readbuf_get_buflen(msg) - roff;
    
    if (!(offset = read_uint16(payload, payload_len, 0, &version)))
	return NULL;
    if (!(offset = read_uint16(payload, payload_len, offset, &type)))
	return NULL;
    if (!(offset = read_uint32(payload, payload_len, offset, &source)))
	return NULL;
    if (!(offset = read_uint32(payload, payload_len, offset, &source_view)))
	return NULL;
    if (!(offset = read_uint64(payload, payload_len, offset, &seq)))
	return NULL;
    if (!(offset = read_uint32(payload, payload_len, offset, &safety)))
	return NULL;
    ret = vs_msg_new(type, source, source_view, seq, safety);
    ret->read_msgbuf = msg;
    ret->read_offset = roff;
    return ret;
}

size_t vs_msg_get_read_offset(const vs_msg_t *msg)
{
    assert(msg != NULL);
    return msg->read_offset;
}

const readbuf_t *vs_msg_get_readbuf(const vs_msg_t *msg)
{
    assert(msg != NULL);
    return msg->read_msgbuf;
}

vs_msg_safety_e vs_msg_get_safety(const vs_msg_t *msg)
{
     assert(msg);
     return msg->safety;
}


size_t vs_msg_get_payload_len(const vs_msg_t *msg)
{
    assert(msg);
    if (msg->read_msgbuf)
	return readbuf_get_buflen(msg->read_msgbuf) - msg->read_offset - 
	    VS_MSG_HDR_LEN;
    return 0;
}

const void *vs_msg_get_payload(const vs_msg_t *msg)
{
    assert(msg);
    return msg->read_msgbuf ? 
	(void *)((char*)readbuf_get_buf(msg->read_msgbuf, msg->read_offset) + 
		 VS_MSG_HDR_LEN) : NULL; 
}

vs_msg_e vs_msg_get_type(const vs_msg_t *msg)
{
    assert(msg);
    return msg->type;
}

addr_t vs_msg_get_source(const vs_msg_t *msg)
{
    assert(msg);
    return msg->source;
}

seq_t vs_msg_get_seq(const vs_msg_t *msg)
{
    assert(msg);
    return msg->seq;
}

vs_view_id_t vs_msg_get_source_view_id(const vs_msg_t *msg)
{
    assert(msg);
    return msg->source_view;
}
