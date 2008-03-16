#define _GNU_SOURCE
#include "gcomm/transport.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <glib.h>



struct transport_ {
    int fd;
    transport_e type;
    transport_state_e state;
    poll_t *poll;
    protolay_t *pl;
    size_t sa_size;
    struct sockaddr sa;
    writebuf_t *s_msg;
    size_t s_offset;
    char *r_buf;
    size_t r_buf_len;
    readbuf_t *r_msg;
    size_t r_offset;
    GQueue *pending;
};

static void free_cb(void *ptr)
{
    transport_free(ptr);
}

static void poll_event_cb(void *ctx, int fd, poll_e ev)
{
    int err;
    transport_t *t = (transport_t *) ctx;
    g_assert(t->fd == fd);

    if (t->state == TRANSPORT_S_CONNECTING && (ev & POLL_OUT)) {
	poll_unset(t->poll, t->fd, POLL_OUT);
	t->state = TRANSPORT_S_CONNECTED;
	protolay_pass_up(t->pl, NULL, 0, NULL);
	
    } else if (t->state == TRANSPORT_S_CONNECTED) {
	if (ev & POLL_IN) {
	    transport_recv(t, NULL);
	} 
	if (ev & POLL_OUT) {
	    switch ((err = transport_send(t, NULL))) {
	    case 0:
		poll_unset(t->poll, t->fd, POLL_OUT);
		break;
	    case EAGAIN:
		/* Do nothing, have to try resend */
		break;
	    default:
		/* Hmm... actual error */
		fprintf(stderr, "Transport failure (%i): '%s'\n",
			err, strerror(err));
		t->state = TRANSPORT_S_FAILED;
		protolay_pass_up(t->pl, NULL, 0, NULL);
	    }
	}
    } else if (t->state == TRANSPORT_S_LISTENING) {
	/* Shouldn't really do this... */
    }
}

transport_t *transport_new(transport_e type,
			   poll_t *poll,
			   protolay_t *up_ctx,
			   void (*pass_up_cb)(protolay_t *,
					      const readbuf_t *,
					      const size_t,
					      const up_meta_t *))
{
    transport_t *t;
    if (type != TRANSPORT_TCP)
	return NULL;
    
    t = malloc(sizeof(transport_t));
    t->type = type;
    t->state = TRANSPORT_S_CLOSED;
    t->poll = poll;
    t->pl = protolay_new(t, &free_cb);
    protolay_set_up(t->pl, up_ctx, pass_up_cb);
    t->fd = -1;
    memset(&t->sa, 0, sizeof(struct sockaddr));
    t->sa_size = 0;
    t->s_msg = NULL;
    t->s_offset = 0;
    t->r_buf_len = 8192;
    t->r_buf = malloc(t->r_buf_len);
    t->r_msg = NULL;
    t->r_offset = 0;
    t->pending = g_queue_new();
    return t;
}

void transport_free(transport_t *t)
{
    GList *i;
    if (t) {
	for (i = g_queue_peek_head_link(t->pending); i; 
	     i = g_list_next(i))
	    writebuf_free(i->data);
	g_queue_free(t->pending);
	writebuf_free(t->s_msg);
	readbuf_free(t->r_msg);
	free(t->r_buf);
	free(t);
    }
}

static bool addr_to_sa(const char *addr, struct sockaddr *s, size_t *s_size)
{
     struct sockaddr_in *sa;
     char *ipaddr;
     char *port;
     const char *delim;
     if (!(delim = strchr(addr, ':')))
	  return false;
     
     ipaddr = strndup(addr, delim - addr);
     port = strdup(delim + 1);
     sa = (struct sockaddr_in *) s;
     if (inet_pton(AF_INET, ipaddr, &sa->sin_addr) <= 0) {
	  free(ipaddr);
	  free(port);
	  return false;
     }
     sa->sin_family = AF_INET;
     sa->sin_port = htons(strtol(port, NULL, 0));
     *s_size = sizeof(struct sockaddr_in);
     free(ipaddr);
     free(port);
     return true;
}

int transport_connect(transport_t *t, const char *addr)
{
    int ret;

    if (t->fd != -1)
	return EPERM;

    if (!addr_to_sa(addr, &t->sa, &t->sa_size)) {
	fprintf(stderr, "addr_to_sa\n");
	return EINVAL;
    }

     
    if ((t->fd = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
	ret = errno;
	fprintf(stderr, "socket %i\n", ret);
	return ret;
    }
     
    if (fcntl(t->fd, F_SETFL, O_NONBLOCK) == -1) {
	ret = errno;
	while (close(t->fd) == -1 && errno == EINTR);
	t->fd = -1;
	fprintf(stderr, "fcntl %i\n", ret);
	return ret;
    }

    if (t->poll)
	poll_insert(t->poll, t->fd, t, &poll_event_cb);
    if (connect(t->fd, &t->sa, sizeof(struct sockaddr_in)) == -1) {
	ret = errno;
	if (ret != EINPROGRESS) {
	    poll_erase(t->poll, t->fd);
	    while (close(t->fd) == -1 && errno == EINTR);
	    t->fd = -1;
	} else {
	    t->state = TRANSPORT_S_CONNECTING;
	    if (t->poll)
		poll_set(t->poll, t->fd, POLL_OUT);
	}
	fprintf(stderr, "connect %i\n", ret);
    }
    return ret;
}

void transport_close(transport_t *t)
{
    if (t->fd != -1) {
	if (t->poll)
	    poll_erase(t->poll, t->fd);
	while (close(t->fd) == -1 && errno == EINTR);
	t->fd = -1;
	t->state = TRANSPORT_S_CLOSED;
    }
}

int transport_listen(transport_t *t, const char *addr)
{
    int ret;
    int reuse = 1;
    
    g_assert(t->poll == NULL);
    
    if (t->fd != -1)
	return EPERM;
    if (!addr_to_sa(addr, &t->sa, &t->sa_size)) {
	fprintf(stderr, "addr_to_sa\n");
	return EINVAL;
    }     
    if ((t->fd = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
	ret = errno;
	fprintf(stderr, "socket %i\n", ret);
	return ret;
    }

    if (fcntl(t->fd, F_SETFL, O_NONBLOCK) == -1) {
	ret = errno;
	while (close(t->fd) == -1 && errno == EINTR);
	t->fd = -1;
	fprintf(stderr, "fcntl %i\n", ret);
	return ret;
    }

    if (setsockopt(t->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
	ret = errno;
	while (close(t->fd) == -1 && errno == EINTR);
	t->fd = -1;
	fprintf(stderr, "setsockopt %i\n", ret);
	return ret;
    }

    if (bind(t->fd, &t->sa, t->sa_size) == -1) {
	ret = errno;
	while (close(t->fd) == -1 && errno == EINTR);
	t->fd = -1;
	fprintf(stderr, "bind %i\n", ret);
	return ret;
    }

    if (listen(t->fd, 128) == -1) {
	ret = errno;
	while (close(t->fd) == -1 && errno == EINTR);
	t->fd = -1;
	fprintf(stderr, "listen %i\n", ret);
	return ret;
    }
    t->state = TRANSPORT_S_LISTENING;
    return 0;
}

int transport_accept(transport_t *t, transport_t **acc, 
		     poll_t *poll,
		     protolay_t *up_ctx,
		     void (*pass_up_cb)(protolay_t *,
					const readbuf_t *,
					const size_t,
					const up_meta_t *))
{
    int fd;
    int ret;
    struct sockaddr sa;
    size_t sa_size = sizeof(struct sockaddr);
    if (!(t && acc))
	return EINVAL;
     
    if ((fd = accept(t->fd, &sa, &sa_size)) == -1) {
	ret = errno;
	return ret;
    }

    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
	ret = errno;
	while (close(fd) == -1 && errno == EINTR);
	fd = -1;
	return ret;
    }
     
    *acc = transport_new(t->type, poll, up_ctx, pass_up_cb);
    (*acc)->sa = sa;
    (*acc)->sa_size = sa_size;
    (*acc)->fd = fd;
    (*acc)->state = TRANSPORT_S_CONNECTED;
    if ((*acc)->poll) {
	poll_insert((*acc)->poll, (*acc)->fd, *acc, &poll_event_cb);
	poll_set((*acc)->poll, (*acc)->fd, POLL_IN);
    }
    return 0;
}

int transport_send(transport_t *t, writebuf_t *msg)
{
    int ret = 0;
    const writebuf_t *m;
    writebuf_t *msg_copy;
    uint32_t len;
    
    if (!t->s_msg && !msg)
	return 0;
    if (t->s_msg && msg && g_queue_get_length(t->pending) > 16)
	return EBUSY;
    
    if (t->s_msg && msg) {
	msg_copy = writebuf_copy(msg);
	len = writebuf_get_totlen(msg_copy) + sizeof(uint32_t);
	write_uint32(len, &len, sizeof(len), 0);
	writebuf_prepend_hdr(msg_copy, &len, sizeof(len));
	g_queue_push_tail(t->pending, msg_copy);
	if (t->poll)
	    poll_set(t->poll, t->fd, POLL_OUT);
	return EAGAIN;
    } else if (msg) {
	len = writebuf_get_totlen(msg) + sizeof(uint32_t);
	write_uint32(len, &len, sizeof(len), 0);
	writebuf_prepend_hdr(msg, &len, sizeof(len));
    }
    
    do {
	ret = t->s_offset;
	m = t->s_msg ? t->s_msg : msg;
	g_assert(t->s_offset < writebuf_get_totlen(m));
	while (t->s_offset < writebuf_get_hdrlen(m) &&
	       (ret = send(t->fd, 
			   (char*)writebuf_get_hdr(m) + t->s_offset, 
			   writebuf_get_hdrlen(m) - t->s_offset,
			   MSG_DONTWAIT)) > 0) {
	    t->s_offset += ret;
	}

	if (ret == -1) {
	    ret = errno;
	    if (msg)
		t->s_msg = writebuf_copy(msg);
	    goto out;
	} else if (ret == 0) {
	    ret = EPIPE;
	    goto out;
	}
	while (t->s_offset < writebuf_get_totlen(m) && 
	       (ret = send(t->fd, 
			   (char *)writebuf_get_payload(m) + 
			   (t->s_offset - writebuf_get_hdrlen(m)), 
			   writebuf_get_payloadlen(m) - 
			   (t->s_offset - writebuf_get_hdrlen(m)), 
			   MSG_DONTWAIT)) > 0) {
	    t->s_offset += ret;
	}
	if (ret == -1) {
	    ret = errno;
	    if (msg)
		t->s_msg = writebuf_copy(msg);
	    goto out;
	} else if (ret == 0) {
	    ret = EPIPE;
	    goto out;
	}
	g_assert(t->s_offset == writebuf_get_totlen(m));
	
	writebuf_free(t->s_msg);
	t->s_msg = NULL;
	t->s_offset = 0;
    } while ((t->s_msg = g_queue_pop_head(t->pending)));
    /* All messages sent successfully, set ret to success */
    ret = 0;
out:
    if (msg)
	writebuf_rollback_hdr(msg, sizeof(uint32_t));
    if (ret == EAGAIN && t->poll)
	poll_set(t->poll, t->fd, POLL_OUT);
    return ret;
}

int transport_recv(transport_t *t, const readbuf_t **msg)
{
    int ret = 0;
    uint32_t len;
    
    if (t->r_offset == 0 && t->r_msg) {
	readbuf_free(t->r_msg);
	t->r_msg = NULL;
    }
    
    if (t->r_offset < sizeof(uint32_t)) {
	ret = recv(t->fd, t->r_buf, sizeof(uint32_t) - t->r_offset, 
		   MSG_DONTWAIT);
	if (ret > 0)
	    t->r_offset += ret;
	if (ret == -1) {
	    ret = errno;
	    return ret;
	} else if (ret == 0) {
	    return EPIPE;
	}
    }
    read_uint32(t->r_buf, sizeof(uint32_t), 0, &len);
    if (t->r_buf_len < len) {
	t->r_buf = realloc(t->r_buf, len);
	t->r_buf_len = len;
    }
    
    while (t->r_offset < len && 
	   (ret = recv(t->fd, t->r_buf + t->r_offset, len - t->r_offset, 
		       MSG_DONTWAIT)) > 0) {
	t->r_offset += ret;
    }
    
    if (ret == -1) {
	ret = errno;
	return ret;
    } else if (ret == 0) {
	return EPIPE;
    }
    
    g_assert(t->r_offset == len);
    t->r_msg = readbuf_new(t->r_buf, t->r_buf_len);
    if (msg)
	*msg = t->r_msg;
    else
	protolay_pass_up(t->pl, t->r_msg, sizeof(uint32_t), NULL);
    t->r_offset = 0;
    return 0;
}

int transport_fd(const transport_t *t)
{
    return t->fd;
}

transport_state_e transport_get_state(const transport_t *t)
{
    return t->state;
}
