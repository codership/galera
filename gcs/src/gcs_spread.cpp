/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*****************************************/
/*  Implementation of Spread GC backend  */
/*****************************************/

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sp.h>

#include <galerautils.h>

#include "gcs_spread.h"
#include "gcs_comp_msg.h"

#define SPREAD_MAX_GROUPS 256
#if (GCS_COMP_MEMB_ID_MAX_LEN < MAX_GROUP_NAME)
#error "GCS_COMP_MEMB_ID_MAX_LEN is smaller than Spread's MAX_GROUP_NAME"
#error "This can make creation of component message impossible."
#endif

typedef struct string_array
{
    int32 max_strings;
    int32 num_strings;
    char  strings[0][MAX_GROUP_NAME];
}
string_array_t;

static string_array_t*
string_array_alloc (const long n)
{
    string_array_t *ret = NULL;

    ret = gu_malloc (sizeof (string_array_t) + n * MAX_GROUP_NAME);
    
    if (ret) {
        ret->max_strings = n;
        ret->num_strings = 0;
    }
    return ret;
}

static void
string_array_free (string_array_t *a)
{
    gu_free (a);
}

typedef enum spread_config
{
    SPREAD_REGULAR,
    SPREAD_TRANSITIONAL
}
spread_config_t;

typedef struct gcs_backend_conn
{
    char *socket;
    char *channel;
    char *priv_name;
    char *priv_group;
    char *sender;
    long  msg_type;
    long  my_id;     /* process ID returned with REG_MEMB message */
    long  config_id;
    //    long  memb_num;
    string_array_t *memb;
    string_array_t *groups;
    gcs_comp_msg_t *comp_msg;
    spread_config_t config; /* type of configuration: regular or trans */
    mailbox mbox;
}
spread_t;

/* this function converts socket address from conventional 
 * "addr:port" notation to Spread's "port@addr" notation */
static long gcs_to_spread_socket (const char const *socket, char **sp_socket)
{
    char  *colon    = strrchr (socket, ':');
    size_t addr_len = colon - socket;
    size_t port_len = strlen (socket) - addr_len - 1;
    char  *sps      = NULL;

    if (!colon) return -EADDRNOTAVAIL;

    sps = (char *) strdup (socket);
    if (!sps) return -ENOMEM;

    memcpy (sps, colon+1, port_len);
    memcpy (sps + port_len + 1, socket, addr_len);
    sps[port_len] = '@';
    *sp_socket = sps;
    return 0;
}

static const char* spread_default_socket = "localhost:4803";

static long spread_create (spread_t**  spread,
                           const char* socket)
{
    long err = 0;
    spread_t *sp = GU_CALLOC (1, spread_t);

    *spread = NULL;
    
    if (!sp) { err = -ENOMEM; goto out0; }

    if (NULL == socket || strlen(socket) == 0)
        socket = spread_default_socket;

    err = gcs_to_spread_socket (socket, &sp->socket);
    if (err < 0) { goto out1; }
    
    sp->priv_name = GU_CALLOC (MAX_PRIVATE_NAME, char);
    if (!sp->priv_name) { err = -ENOMEM; goto out3; }
    
    sp->priv_group = GU_CALLOC (MAX_GROUP_NAME, char);
    if (!sp->priv_group) { err = -ENOMEM; goto out4; }
    
    sp->sender = GU_CALLOC (MAX_GROUP_NAME, char);
    if (!sp->sender) { err = -ENOMEM; goto out5; }
    
    sp->groups = string_array_alloc (SPREAD_MAX_GROUPS);
    if (!sp->groups) { err = -ENOMEM; goto out6; }

    sp->memb   = string_array_alloc (SPREAD_MAX_GROUPS);
    if (!sp->memb) { err = -ENOMEM; goto out7; }

    sp->config    = SPREAD_TRANSITIONAL;
    sp->config_id = -1;
    sp->comp_msg  = NULL;
    
    gu_debug ("sp->priv_group: %p", sp->priv_group);
    *spread = sp;
    return err;

out7:
    string_array_free (sp->groups);
out6:
    gu_free (sp->sender);
out5:
    gu_free (sp->priv_group);
out4:
    gu_free (sp->priv_name);
out3:
    free (sp->socket);
out1:
    gu_free (sp);
out0:
    return err;
}

/* Compiles a string of MAX_PRIVATE_NAME characters
   out of a supplied string and a number, returns -1 if digits overflow */
long spread_priv_name (char *name, const char *string, long n)
{
    /* must make sure that it does not overflow MAX_PRIVATE_NAME */
    long max_digit  = 2;
    long max_string = MAX_PRIVATE_NAME - max_digit;
    long len        = snprintf (name, max_string + 1, "%s", string);
    if (len > max_string) len = max_string; // truncated
    gu_debug ("len = %d, max_string = %d, MAX_PRIVATE_NAME = %d\n",
              len, (int)max_string, MAX_PRIVATE_NAME);
    len = snprintf (name + len, max_digit + 1, "_%d", (int)n);
    if (len > max_digit) return -1; // overflow

    return 0;
}

static
GCS_BACKEND_CLOSE_FN(spread_close)
{
    long err = 0;
    spread_t *spread = backend->conn;
    
    if (!spread) return -EBADFD;

    err = SP_leave (spread->mbox, spread->channel);

    if (err)
    {
	switch (err)
	{
	case ILLEGAL_GROUP:     return -EADDRNOTAVAIL;
	case ILLEGAL_SESSION:   return -ENOTCONN;
	case CONNECTION_CLOSED: return -ECONNRESET;
	default:                return -EOPNOTSUPP;
	}
    }
    else
    {
	return 0;
    }
}

static
GCS_BACKEND_DESTROY_FN(spread_destroy)
{
    long err = 0;
    spread_t *spread = backend->conn;
    
    if (!spread) return -EBADFD;

    err = SP_disconnect (spread->mbox);

    if (spread->memb)       string_array_free (spread->memb);
    if (spread->groups)     string_array_free (spread->groups);
    if (spread->sender)     gu_free (spread->sender);
    if (spread->priv_name)  gu_free (spread->priv_name);
    if (spread->priv_group) gu_free (spread->priv_group);
    if (spread->channel)    free (spread->channel); // obtained by strdup()
    if (spread->socket)     free (spread->socket);
    if (spread->comp_msg)   gcs_comp_msg_delete(spread->comp_msg);
    gu_free (spread);

    backend->conn = NULL;

    if (err)
    {
	switch (err)
	{
	case ILLEGAL_GROUP:     return -EADDRNOTAVAIL;
	case ILLEGAL_SESSION:   return -ENOTCONN;
	case CONNECTION_CLOSED: return -ECONNRESET;
	default:                return -EOPNOTSUPP;
	}
    }
    else
    {
	return 0;
    }
}

static
GCS_BACKEND_SEND_FN(spread_send)
{
    long ret = 0;
    spread_t *spread = backend->conn;

    if (SPREAD_TRANSITIONAL == spread->config) return -EAGAIN;

    /* can it be that not all of the message is sent? */
    ret = SP_multicast (spread->mbox,         // mailbox
			SAFE_MESS,            // service type
			spread->channel,      // destination group
			(short)msg_type,      // message from application
			len,                  // message length
			(const char*)buf      // message buffer
			);

    if (ret != len)
    {
        if (ret > 0) return -ECONNRESET; /* Failed to send the whole message */

	switch (ret)
	{
	case ILLEGAL_SESSION:
            return -ENOTCONN;
	case CONNECTION_CLOSED:
            return -ECONNRESET;
	default:
            return -EOPNOTSUPP;
	}
    }

#ifdef GCS_DEBUG_SPREAD
//    gu_debug ("spread_send: message sent: %p, len: %d\n", buf, ret);
#endif
    return ret;
}

/* Substitutes old member array for new (taken from groups),
 * creates new groups buffer. */
static inline long
spread_update_memb (spread_t* spread)
{
    string_array_t* new_groups = string_array_alloc (SPREAD_MAX_GROUPS);

    if (!new_groups) return -ENOMEM;
    string_array_free (spread->memb);
    spread->memb   = spread->groups;
    spread->groups = new_groups;
    return 0;
}

/* Temporarily this is done by simple iteration through the whole list.
 * for a cluster of 2-3 nodes this is probably most optimal. 
 * But it clearly needs to be improved. */
static inline long
spread_sender_id (const spread_t* const spread,
		  const char*     const sender_name)
{
    long id;

    for (id = 0; id < spread->memb->num_strings; id++) {
        if (!strncmp(sender_name,
                     spread->memb->strings[id],
                     MAX_GROUP_NAME))
            return id;
    }

    return GCS_SENDER_NONE;
}

static gcs_comp_msg_t*
spread_comp_create (long  my_id,
		    long  config_id,
		    long  memb_num,
		    char  names[][MAX_GROUP_NAME])
{
    gcs_comp_msg_t* comp  = gcs_comp_msg_new (memb_num > 0, my_id, memb_num);
    long ret = -ENOMEM;

    if (comp) {
	long i;
	for (i = 0; i < memb_num; i++) {
	    ret = gcs_comp_msg_add (comp, names[i]);
	    if (ret != i) {
		gcs_comp_msg_delete (comp);
		goto fatal;
	    }
	}
        gu_debug ("Created a component message of length %d.",
                  gcs_comp_msg_size(comp));
	return comp;
    }

 fatal:
    gu_fatal ("Failed to allocate component message: %s", strerror(-ret));
    return NULL;
}

/* This function actually finalizes component message delivery:
 * it makes sure that the caller will receive the message and only then
 * changes handle state (spread->config)*/
static long
spread_comp_deliver (spread_t* spread,
                     void* buf, long len, gcs_msg_type_t* msg_type)
{
    long ret;

    assert (spread->comp_msg);

    ret = gcs_comp_msg_size (spread->comp_msg);
    if (ret <= len) {
	memcpy (buf, spread->comp_msg, ret);
	spread->config = SPREAD_REGULAR;
        gcs_comp_msg_delete (spread->comp_msg);
	spread->comp_msg = NULL;
        *msg_type      = GCS_MSG_COMPONENT;
	gu_debug ("Component message delivered (length %ld)", ret);
    }
    else {
        // provided buffer is too small for a message:
        // simply return required size
    }

    return ret;
}

static
GCS_BACKEND_RECV_FN(spread_recv)
{
    long      ret = 0;
    spread_t *spread = backend->conn;
    service   serv_type;
    int16     mess_type;
    int32     endian_mismatch;
 
    /* in case of premature exit */
    *sender_idx = GCS_SENDER_NONE;
    *msg_type   = GCS_MSG_ERROR;

    if (spread->comp_msg) { /* undelivered regular component message */
	return spread_comp_deliver (spread, buf, len, msg_type);
    }

    if (!len) { // Spread does not seem to tolerate 0-sized buffer
        return 4096;
    }

    while (1) /* Loop while we don't receive the right message */
    { 
	ret = SP_receive (spread->mbox,     // mailbox/connection
			  &serv_type,       // service type:
			                    // REGULAR_MESS/MEMBERSHIP_MESS
			  spread->sender,   // private group name of a sender
			  spread->groups->max_strings,
			  &spread->groups->num_strings,
			  spread->groups->strings,
			  &mess_type,       // app. defined message type
			  &endian_mismatch,
			  len,              // maximum message length
			  (char*)buf        // message buffer
	    );

//	gcs_log ("gcs_spread_recv: SP_receive returned\n");
//	gcs_log ("endian_mismatch = %d\n", endian_mismatch);
//	/* seems there is a bug in either libsp or spread daemon */
//	if (spread->groups->num_strings < 0 && ret > 0)
//	    ret = GROUPS_TOO_SHORT;
	
	/* First, handle errors */
	if (ret < 0) {
	    switch (ret)
	    {
	    case BUFFER_TOO_SHORT: {
                if (Is_membership_mess (serv_type)) {
                    // Ignore this error as membership messages don't fill
                    // the buffer. Spread seems to have a bug - it returns
                    // BUFFER_TOO_SHORT if you pass zero-length buffer for it.
                    gu_debug ("BUFFER_TOO_SHORT in membership message.");
                    ret = 0;
                    break;
                }
		/* return required buffer size to caller */
                gu_debug ("Error in SP_receive: BUFFER_TOO_SHORT");
                gu_debug ("Supplied buffer len: %d, required: %d",
                          len, (int) -endian_mismatch);
                gu_debug ("Message type: %d, sender: %d",
                          mess_type, spread_sender_id (spread, spread->sender));
		return -endian_mismatch;
	    }
	    case GROUPS_TOO_SHORT: {
		/* reallocate groups */
		size_t num_groups = -spread->groups->num_strings;
                gu_warn ("Error in SP_receive: GROUPS_TOO_SHORT. "
                         "Expect failure.");
		string_array_free (spread->groups);
		spread->groups = string_array_alloc (num_groups);
		if (!spread->groups) return -ENOMEM;
		/* try again */
		continue;
	    }
	    case ILLEGAL_SESSION:
                gu_debug ("Error in SP_receive: ILLEGAL_SESSION");
                return -ECONNABORTED;
	    case CONNECTION_CLOSED:
                gu_debug ("Error in SP_receive: CONNECTION_CLOSED");
                return -ECONNABORTED;
	    case ILLEGAL_MESSAGE:
                gu_debug ("Error in SP_receive: ILLEGAL_MESSAGE");
                continue; // wait for a legal one?
	    default:
		gu_fatal ("unknown error = %d", ret);
		return -ENOTRECOVERABLE;
	    }
	}

	/* At this point message was successfully received
         * and stored in buffer. */
	
	if (Is_regular_mess (serv_type))
	{
//	    gu_debug ("received REGULAR message of type %d\n",
//                mess_type);

	    assert (endian_mismatch >= 0); /* BUFFER_TOO_SMALL
					    * must be handled before */
	    if (endian_mismatch) {
		gu_debug ("Spread returned ENDIAN_MISMATCH. Ignored.");
	    }
	    *msg_type  = mess_type;
            *sender_idx = spread_sender_id (spread, spread->sender);
            assert (*sender_idx >= 0);
            assert (*sender_idx < spread->memb->num_strings);
	    break;
	}
	else if (Is_membership_mess (serv_type))
	{
	    if (strncmp (spread->channel, spread->sender, MAX_GROUP_NAME))
		continue; // wrong group/channel
	    if (Is_transition_mess (serv_type)) {
		spread->config   = SPREAD_TRANSITIONAL;
		gu_info ("Received TRANSITIONAL message");
		continue;
	    }
	    else if (Is_reg_memb_mess (serv_type)) {
		//assert (spread->groups->num_strings > 0);
		spread->my_id  = mess_type;
		gu_info ("Received REGULAR MEMBERSHIP "
			 "in group \'%s\' with %d(%d) members "
			 "where I'm member %d\n",
			 spread->sender,
			 spread->groups->num_strings,
			 spread->groups->max_strings,
			 spread->my_id);
		spread->config_id++;
		gu_debug ("Configuration number: %d", spread->config_id);
		spread->comp_msg = spread_comp_create (spread->my_id,
						   spread->config_id,
						   spread->groups->num_strings,
						   spread->groups->strings);
		if (!spread->comp_msg) return -ENOTRECOVERABLE;

                /* Update membership info */
                if ((ret = spread_update_memb(spread))) return ret;

		if (Is_caused_join_mess (serv_type)) {
		    gu_info ("due to JOIN");
		}
		else if (Is_caused_leave_mess (serv_type)) {
		    gu_info ("due to LEAVE");
		}
		else if (Is_caused_disconnect_mess (serv_type)) {
		    gu_info ("due to DISCONNECT");
		}
		else if (Is_caused_network_mess (serv_type)) {
		    gu_info ("due to NETWORK");
		}
		else {
		    gu_warn ("unknown REG_MEMB message");
		}
		ret = spread_comp_deliver (spread, buf, len, msg_type);
	    }
	    else if (Is_caused_leave_mess (serv_type)) {
		gu_info ("received SELF LEAVE message");
//		*msg_type = GCS_MSG_COMPONENT;
//		memset (buf, 0, len); // trivial component
		spread->comp_msg = gcs_comp_msg_leave ();
		ret = spread_comp_deliver (spread, buf, len, msg_type);
	    }
	    else {
		gu_warn ("received unknown MEMBERSHIP message");
		continue; // must do something ???
	    }
	}
	else if (Is_reject_mess (serv_type))
	{
	    gu_info ("received REJECTED message form %s",
		     spread->sender);
	    continue;
	}
	else /* Unknown message type */
	{
	    gu_warn ("received message of unknown type");
	    continue;
	}

	/* If we reached this point we have successfully received a message */
	break;
    }

    /* message is already in buf and its length in ret */
    return ret;
}

static
GCS_BACKEND_NAME_FN(spread_name)
{
    static char str[128];
    int maj, min, patch;
    SP_version (&maj, &min, &patch);
    snprintf (str, 128, "Spread %d.%d.%d", maj, min, patch);
    return str;
}

/* Spread packet structure seem to be:
 * 42 bytes - Ethernet + IP + UDP header, 32 bytes Spread packet header +
 * 80 byte Spread message header present only in the first packet */
static
GCS_BACKEND_MSG_SIZE_FN(spread_msg_size)
{
    long ps = pkt_size;
    long frames = 0;
    const long eth_frame_size      = 1514;
    const long spread_header_size  = 154;   // total headers in Spread packet
    const long spread_max_pkt_size = 31794; // 21 Ethernet frames

    if (pkt_size <= spread_header_size) {
        ps = spread_header_size + 1;
	gu_warn ("Requested packet size %d is too small, "
                 "minimum possible is %d", pkt_size, ps);
        return pkt_size - ps;
    }

    if (pkt_size > spread_max_pkt_size) {
        ps = spread_max_pkt_size;
	gu_warn ("Requested packet size %d is too big, "
		 "using maximum possible: %d", pkt_size, ps);
    }

    frames = ps / eth_frame_size;
    frames += ((frames * eth_frame_size) < ps); // one incomplete frame

    return (ps - frames * (42 + 32) - 80);
}

static
GCS_BACKEND_OPEN_FN(spread_open)
{
    long      err    = 0;
    spread_t* spread = backend->conn;
    
    if (!spread) return -EBADFD;

    if (!channel) {
	gu_error ("No channel supplied.");
	return -EINVAL;
    }

    spread->channel = strdup (channel);
    if (!spread->channel) return -ENOMEM;
    
    err = SP_join (spread->mbox, spread->channel);

    if (err)
    {
	switch (err) /* translate error codes */
	{
	case ILLEGAL_GROUP:     err = -EADDRNOTAVAIL; break; 
	case ILLEGAL_SESSION:   err = -EADDRNOTAVAIL; break;
	case CONNECTION_CLOSED: err = -ENETRESET;     break;
	default:                err = -ENOTCONN;      break;
	}
        gu_error ("%s", strerror (-err));
	return err;
    }

    gu_info ("Joined channel: %s", spread->channel);

    return err;
}

#if defined(__linux__)
extern char *program_invocation_short_name;
#endif

GCS_BACKEND_CREATE_FN(gcs_spread_create)
{
    long      err    = 0;
    long      n      = 0;
    spread_t* spread = NULL;

    backend->conn    = NULL;
    
    if (!socket) {
	gu_error ("No socket supplied.");
	err = -EINVAL;
	goto out0;
    }

    if ((err = spread_create (&spread, socket))) goto out0;

    do
    {   /* Try to generate unique name */
	if (spread_priv_name (spread->priv_name,
#if defined(__sun__)
                              getexecname (),
#elif defined(__APPLE__) || defined(__FreeBSD__)
                              getprogname (),
#elif defined(__linux__)
                              program_invocation_short_name,
#else
                              "unknown",
#endif
                              n++))
	{
	    /* Failed to generate a name in the form
	     * program_name_number. Let spread do it for us */
	    gu_free (spread->priv_name);
	    spread->priv_name = NULL;
	}

	err = SP_connect (spread->socket, spread->priv_name, 0, 1,
			  &spread->mbox, spread->priv_group);
    }
    while (REJECT_NOT_UNIQUE == err);

    if (err < 0)
    {
        gu_debug ("Spread connect error");
	switch (err) /* translate error codes */
	{
	case ILLEGAL_SPREAD:
	    err = -ESOCKTNOSUPPORT; break; 
	case COULD_NOT_CONNECT:
	    err = -ENETUNREACH; break;
	case CONNECTION_CLOSED:
	    err = -ENETRESET; break;
	case REJECT_ILLEGAL_NAME:
	    err = -EADDRNOTAVAIL;
	    gu_error ("Spread returned REJECT_ILLEGAL_NAME");
	    break;
	case REJECT_NO_NAME:
	    err = -EDESTADDRREQ;
	    gu_error ("Spread returned REJECT_NO_NAME."
		       "Spread protocol error");
	    break;
	case REJECT_VERSION:
	default:
	    gu_error ("Generic Spread error code: %d", err);
	    err = -EPROTONOSUPPORT;
	    break;
	}
	goto out1;
    }
    else {
        assert (err == ACCEPT_SESSION);
        err = 0;
    }

    gu_debug ("Connected to Spread: priv_name = %s, priv_group = %s",
	     spread->priv_name, spread->priv_group);

    backend->conn     = spread;
    backend->open     = spread_open;
    backend->close    = spread_close;
    backend->send     = spread_send;
    backend->recv     = spread_recv;
    backend->name     = spread_name;
    backend->msg_size = spread_msg_size;
    backend->destroy  = spread_destroy;

    return err;

out1:
    spread_destroy (backend);
out0:
    gu_error ("Creating Spread backend failed: %s (%d)",
              strerror (-err), err);
    return err;
}

