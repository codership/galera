/**
 * @file msg.h
 *
 * Generic message framework
 *
 * Author: Teemu Ollakka <teemu.ollakka@codership.com>
 *
 * Copyright (C) 2007 Codership Oy <info@codership.com>
 */
#ifndef MSG_H
#define MSG_H

#include <gcomm/types.h>

/**
 * Forward declaration for message type. 
 */ 
typedef struct msg_ msg_t;

/**
 * Create new message
 */
msg_t *msg_new(void);

/**
 * Free message
 */
void msg_free(msg_t *);

/**
 * Create copy of message
 */
msg_t *msg_copy(const msg_t *msg);


/**
 * Get total length of message 
 */
size_t msg_get_len(const msg_t *msg);

size_t msg_write(const msg_t *msg, void *buf, const size_t buflen);
msg_t *msg_read(const void *buf, const size_t buflen);
/**
 * Set message header and payload pointers. Paylaod will be set to location
 * hdr + hdr_len.
 *
 * @param msg
 * @param hdr Pointer to start of header
 * @param hdr_len Length of header
 * @param total_len Total length of buffer
 *
 * @return true or false.
 */
bool msg_set(msg_t *msg, const void *hdr, const size_t hdr_len, const size_t total_len);

/**
 * Set message header
 */
bool msg_set_hdr(msg_t *msg, const void *hdr, const size_t hdr_len);

bool msg_append_hdr(msg_t *msg, const void *hdr, const size_t hdr_len);

/**
 * Prepend header to message. This is usually called by lower layers 
 * when sending message. 
 */
bool msg_prepend_hdr(msg_t *msg, const void *hdr, const size_t hdr_len);

/**
 * Get total length of header buffer
 */
size_t msg_get_hdr_len(const msg_t *msg);

/**
 * Get const pointer to header buffer.
 */
const void *msg_get_hdr(const msg_t *msg);

/**
 * Set message payload to point in some location,
 */
bool msg_set_payload(msg_t *msg, const void *payload, const size_t payload_len);

/**
 * Get message payload length
 */
size_t msg_get_payload_len(const msg_t *msg);

/**
 * Get pointer to message payload
 */
const void *msg_get_payload(const msg_t *msg);


#endif /* MSG_H */
