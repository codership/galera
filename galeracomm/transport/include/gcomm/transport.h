#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <gcomm/readbuf.h>
#include <gcomm/writebuf.h>
#include <gcomm/protolay.h>

typedef enum {
     TRANSPORT_NONE,
     TRANSPORT_TCP
} transport_e;

typedef enum {
    TRANSPORT_S_CLOSED,
    TRANSPORT_S_CONNECTING,
    TRANSPORT_S_CONNECTED,
    TRANSPORT_S_LISTENING,
    TRANSPORT_S_FAILED
} transport_state_e;

typedef struct transport_ transport_t;

transport_t *transport_new(transport_e, 
			   poll_t *poll,
			   protolay_t *up_ctx, 
			   void (*pass_up_cb)(protolay_t *,
					      const readbuf_t *,
					      const size_t,
					      const up_meta_t *));

void transport_free(transport_t *);

int transport_connect(transport_t *, const char *addr);
void transport_close(transport_t *);

int transport_listen(transport_t *, const char *addr);

int transport_accept(transport_t *, transport_t **,
		     poll_t *poll,
		     protolay_t *up_ctx, 
		     void (*pass_up_cb)(protolay_t *,
					const readbuf_t *,
					const size_t,
					const up_meta_t *));

int transport_send(transport_t *, writebuf_t *);
int transport_recv(transport_t *, const readbuf_t **);

int transport_fd(const transport_t *);
transport_state_e transport_get_state(const transport_t *);

#endif /* TRANSPORT_H */
