// Copyright (C) 2007 Codership Oy <info@codership.com>
/*
 * This header defines generic communication layer
 * which implements basic open/close/send/receive
 * functions. Its purpose is to implement all
 * functionality common to all group communication
 * uses. Currently this amounts to action
 * fragmentation/defragmentation and invoking backend
 * functions.
 * In the course of development it has become clear
 * that such fuctionality must be collected in a
 * separate layer.
 * Application abstraction layer is based on this one
 * and uses those functions for its own purposes.
 */

#ifndef _gcs_generic_h_
#define _gcs_generic_h_

#include <stdint.h>
#include <stdlib.h>
#include "gcs.h"

struct gcs_generic_conn;
typedef struct gcs_generic_conn gcs_generic_conn_t; 

/*
 * gcs_generic_open() initialises connection context private to
 * generic communicaton layer - send/recieve buffers and the like.
 * Return values:
 * zero     - success
 * negative - error code
 */
long gcs_generic_open  (gcs_generic_conn_t **conn,
			const char* const channel,
			const char* const socket,
			gcs_backend_type_t const backend);

/*
 * gcs_generic_close() frees resources allocated by gcs_geenric_open()
 * Return values:
 * zero     - success
 * negative - error code
 */
long gcs_generic_close (gcs_generic_conn_t **conn);

/* 
 * gcs_generic_send() atomically sends action to group.
 * Return values:
 * non-negative - amount of action bytes sent (sans headers)
 * negative     - error code
 */
ssize_t gcs_generic_send (gcs_generic_conn_t* const conn,
			  const uint8_t*            action,
			  size_t                    act_size,
			  gcs_act_type_t      const act_type);
/*
 * gcs_generic_recv() blocks until some action is returned from group.
 * Return values:
 * non-negative - the size of action received
 * negative     - error code
 */
ssize_t gcs_generic_recv (gcs_generic_conn_t* const conn,
			  uint8_t**           const action,
			  gcs_act_type_t*     const act_type,
			  gcs_seqno_t*        const act_id);

/* Configuration functions */
/* Sets maximum message size to achieve requested network packet size. 
 * In case of failure returns -EMSGSIZE */
long gcs_generic_set_pkt_size (gcs_generic_conn_t *conn, ulong pkt_size);
#endif // _gcs_generic_h_
