#define _GNU_SOURCE
#include "galeracomm/poll.h"
#include "galeracomm/transport.h"
#include "galeracomm/vs_msg.h"

#include "vs_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <assert.h>
#include <errno.h>

typedef struct evs_client_ {
     bool failed;
     vs_backend_t *be;
     transport_t *tp;
} evs_client_t;

evs_client_t *evs_client_new(transport_t *tp)
{
     evs_client_t *c;

     c = malloc(sizeof(evs_client_t));
     c->failed = false;
     c->tp = tp;
     c->be = vs_backend_new("shm:test", NULL, NULL);
     return c;
}

void evs_client_free(evs_client_t *c)
{
     if (c) {
	  transport_free(c->tp);
	  vs_backend_free(c->be);
	  free(c);
     }
     
}

typedef struct evs_server_ {
     transport_t *listen;
     poll_t *poll;
     vs_backend_t *be;
     GList *clients;
     GList *opening_clients;
     GList *closing_clients;
     bool running;
} evs_server_t;

evs_server_t evs_server = {NULL, NULL, NULL, NULL, NULL, false};


void accept_cb(void *ctx, int fd, poll_e ev)
{
     int ret;
     transport_t *tp = NULL;
     evs_client_t *c;
     if (ev & POLL_IN) {
	  if ((ret = transport_accept(evs_server.listen, &tp))) {
	       fprintf(stderr, "Accept failed: %i\n", ret);
	  } else {
	       c = evs_client_new(tp);
	       evs_server.opening_clients = g_list_append(
		    evs_server.opening_clients, c);
	  }
     } else {
	  fprintf(stderr, "Unknown poll event %i\n", ev);
	  evs_server.running = false;
     }
}

void client_poll_cb(void *ctx, int fd, poll_e ev)
{
     int ret;
     evs_client_t *c = (evs_client_t *) ctx;
     const msg_t *msg = NULL;
     vs_msg_t *vs_msg;

     assert(transport_fd(c->tp) == fd);     
     if (ev & POLL_IN) {
	  ret = transport_recv(c->tp, &msg);
	  if (ret == 0) {
	       if (!(vs_msg = vs_msg_read(msg))) {
		    c->failed = true;
		    fprintf(stderr, "Vs msg read failed\n");
		    evs_server.closing_clients = 
			 g_list_append(evs_server.closing_clients, c);
		    return;
	       } else {
		    vs_backend_send(evs_server.be, vs_msg);
	       }
	       vs_msg_free(vs_msg);
	  } else if (ret != EAGAIN) {
	       c->failed = true;
	       fprintf(stderr, "Client read failed: %i\n", ret);
	       evs_server.closing_clients = 
		    g_list_append(evs_server.closing_clients, c);
	       return;
	  }
     }
     
     if (ev & POLL_OUT) {
	  ret = transport_send(c->tp, NULL);
	  if (ret == 0) {
	       poll_unset(evs_server.poll, transport_fd(c->tp), POLL_OUT);
	  } else if (ret != EAGAIN) {
	       c->failed = true;
	       fprintf(stderr, "Client send failed: %i\n", ret);
	       evs_server.closing_clients = 
		    g_list_append(evs_server.closing_clients, c);
	       return;	       
	  }
     }
     if (ev & POLL_ERR) {
	  c->failed = true;
	  fprintf(stderr, "Poll err\n");
	  evs_server.closing_clients = 
	       g_list_append(evs_server.closing_clients, c);
	  return;
     }
}

void client_sched_cb(void *ctx, const vs_msg_t *msg)
{
     int ret;
     evs_client_t *c = (evs_client_t *) ctx;
     msg_t *send_msg;
     const msg_t *readbuf;

     if (c->failed)
	  return;

     send_msg = msg_new();
     readbuf = vs_msg_get_readbuf(msg);
     msg_set_payload(send_msg, (char *)msg_get_payload(readbuf) + msg_get_hdr_len(readbuf),
		     msg_get_payload_len(readbuf) - msg_get_hdr_len(readbuf));
     ret = transport_send(c->tp, send_msg);
     msg_free(send_msg);
     
     if (ret == EAGAIN)
	  poll_set(evs_server.poll, transport_fd(c->tp), POLL_OUT);
}

void handle_opening()
{
     GList *i;
     evs_client_t *c;
     for (i = g_list_first(evs_server.opening_clients); i; 
	  i = g_list_first(evs_server.opening_clients)) {
	  c = i->data;
	  evs_server.opening_clients = g_list_remove(evs_server.opening_clients, c);	  
	  poll_insert(evs_server.poll, transport_fd(c->tp), c, 
		      &client_poll_cb);
	  poll_set(evs_server.poll, transport_fd(c->tp), POLL_IN);
	  vs_backend_connect(c->be, 0);
	  vs_backend_set_recv_cb(c->be, c, &client_sched_cb);

     }
}

void handle_closing()
{
     GList *i;
     evs_client_t *c;
     for (i = g_list_first(evs_server.opening_clients); i; 
	  i = g_list_first(evs_server.opening_clients)) {
	  c = i->data;
	  evs_server.closing_clients = 
	       g_list_remove(evs_server.closing_clients, c);
	  poll_erase(evs_server.poll, transport_fd(c->tp));
	  vs_backend_disconnect(c->be);
	  transport_close(c->tp);
	  evs_client_free(c);

     }
}

void schedule()
{
     while (vs_backend_sched(evs_server.be, NULL, -1) == 1);
}

int main(int argc, char *argv[]) 
{
     int i;
     int ret;
     const char *listen_addr = NULL;
     
     for (i = 1; i < argc; i++) {
	  if (strncmp(argv[i], "--listen-addr=", 
		      strlen("--listen-addr=")) == 0) {
	       listen_addr = argv[i] + strlen("--listen-addr=");
	  }
     }

     if (!listen_addr) {
	  fprintf(stderr, "--listen-addr is mandatory\n");
	  return EXIT_FAILURE;
     }
     
     evs_server.listen = transport_new(TRANSPORT_TCP);
     if ((ret = transport_listen(evs_server.listen, listen_addr))) {
	  fprintf(stderr, "Listen failed: %i\n", ret);
	  transport_free(evs_server.listen);
	  return EXIT_FAILURE;
     }
     
     evs_server.poll = poll_new();
     poll_insert(evs_server.poll, transport_fd(evs_server.listen), 
		 &evs_server, &accept_cb);
     poll_set(evs_server.poll, transport_fd(evs_server.listen), POLL_IN);

     evs_server.be = vs_backend_new("shm:test", NULL, NULL);

     evs_server.running = true;
     do {
	  if (poll_until(evs_server.poll, 1000)) 
	       fprintf(stderr, "Poll failed: %i\n", ret);
	  handle_opening();
	  handle_closing();
	  schedule();
     } while (evs_server.running == true);
     
     vs_backend_free(evs_server.be);
     poll_erase(evs_server.poll, transport_fd(evs_server.listen));
     transport_close(evs_server.listen);
     transport_free(evs_server.listen);

     return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
