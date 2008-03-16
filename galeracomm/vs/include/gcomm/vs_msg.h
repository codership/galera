#ifndef VS_MSG_H
#define VS_MSG_H

#include <gcomm/addr.h>
#include <gcomm/readbuf.h>
#include <gcomm/seq.h>
#include <gcomm/vs_view.h>

typedef enum {
     VS_MSG_NONE,
     VS_MSG_REG_CONF,
     VS_MSG_TRANS_CONF,
     VS_MSG_STATE,
     VS_MSG_DATA,
     VS_MSG_ERR
} vs_msg_e;

typedef enum {
     VS_MSG_SAFETY_NONE,
     VS_MSG_SAFETY_CAUSAL, 
     VS_MSG_SAFETY_SAFE
} vs_msg_safety_e;

typedef struct vs_msg_ vs_msg_t;



/**
 * Create new VS message 
 */
vs_msg_t *vs_msg_new(const vs_msg_e type, 
		     const addr_t source,
		     const vs_view_id_t source_view,
		     const seq_t seq,
		     const vs_msg_safety_e safety);

/**
 * Free VS msg
 */
void vs_msg_free(vs_msg_t *);

/**
 * Get serialized message header
 */
const void *vs_msg_get_hdr(const vs_msg_t *);

/**
 * Get length of serialized message header
 */
size_t vs_msg_get_hdrlen(const vs_msg_t *);

/**
 * Read message from readbuf
 */
vs_msg_t *vs_msg_read(const readbuf_t *, const size_t roff);

/**
 * Create copy from vs message
 */
vs_msg_t *vs_msg_copy(const vs_msg_t *);

size_t vs_msg_get_read_offset(const vs_msg_t *);
const readbuf_t *vs_msg_get_readbuf(const vs_msg_t *);

vs_msg_safety_e vs_msg_get_safety(const vs_msg_t *);
size_t vs_msg_get_payload_len(const vs_msg_t *);
const void *vs_msg_get_payload(const vs_msg_t *);
vs_msg_e vs_msg_get_type(const vs_msg_t *);
addr_t vs_msg_get_source(const vs_msg_t *);
seq_t vs_msg_get_seq(const vs_msg_t *);
vs_view_id_t vs_msg_get_source_view_id(const vs_msg_t *);

static inline bool vs_msg_is_conf(const vs_msg_t *msg) 
{
    return vs_msg_get_type(msg) == VS_MSG_TRANS_CONF ||
	vs_msg_get_type(msg) == VS_MSG_REG_CONF;
}

static inline bool vs_msg_is_user(const vs_msg_t *msg) 
{
    return !vs_msg_is_conf(msg);
}

#endif /* VS_MSG_H */
