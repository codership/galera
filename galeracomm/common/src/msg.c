
#include "gcomm/msg.h"

#include <stdlib.h>
#include <glib.h>
#include <stdio.h>


struct msg_ {
     volatile int refcnt;
     const void *hdr;
     void *priv_hdr;
     size_t hdr_len;
     const void *payload;
     void *priv_payload;
     size_t payload_len;
};

msg_t *msg_new()
{
     msg_t *msg;
     msg = g_malloc(sizeof(msg_t));
     msg->refcnt = 1;
     msg->hdr = NULL;
     msg->priv_hdr = NULL;
     msg->hdr_len = 0;
     msg->payload = NULL;
     msg->priv_payload = NULL;
     msg->payload_len = 0;
     return msg;
}

msg_t *msg_copy(const msg_t *msg)
{
     msg_t *ncmsg;
     
     ncmsg = (msg_t *) msg;
     if (g_atomic_int_compare_and_exchange(&ncmsg->refcnt, 1, 2)) {
	  if (!ncmsg->priv_hdr && ncmsg->hdr) {
	       ncmsg->priv_hdr = g_malloc(msg->hdr_len);
	       memcpy(ncmsg->priv_hdr, msg->hdr, msg->hdr_len);	       
	       ncmsg->hdr = ncmsg->priv_hdr;
	  }
	  if (!ncmsg->priv_payload && ncmsg->payload) {
	       ncmsg->priv_payload = g_malloc(msg->payload_len);
	       memcpy(ncmsg->priv_payload, msg->payload, msg->payload_len);
	       ncmsg->payload = ncmsg->priv_payload;
	  }
     } else {
	  g_atomic_int_add(&ncmsg->refcnt, 1);
     }
     return ncmsg;
}

void msg_free(msg_t *msg)
{
     if (!msg)
	  return;
     if (g_atomic_int_compare_and_exchange(&msg->refcnt, 1, 0)) {
	  g_free(msg->priv_hdr);
	  g_free(msg->priv_payload);
	  g_free(msg);
     } else {
	  g_atomic_int_add(&msg->refcnt, -1);
     }
}

size_t msg_get_len(const msg_t *msg)
{
     return msg->hdr_len + msg->payload_len;
}

size_t msg_write(const msg_t *msg, void *buf, const size_t buflen)
{
     size_t len;
     len = msg_get_len(msg);
     if (buflen < len)
	  return 0;
     memcpy(buf, msg->hdr, msg->hdr_len);
     memcpy((char*)buf + msg->hdr_len, msg->payload, msg->payload_len);
     return len;
}

msg_t *msg_read(const void *buf, const size_t buflen)
{
     msg_t *msg;
     msg = msg_new();
     msg_set_payload(msg, buf, buflen);
     return msg;
}

bool msg_set(msg_t *msg, const void *hdr, const size_t hdr_len, const size_t total_len)
{
     if (hdr_len > total_len)
	  return false;
     if (!msg_set_hdr(msg, hdr, hdr_len))
	  return false;
     if (!msg_set_payload(msg, (char *)hdr + msg_get_hdr_len(msg), 
			  total_len - msg_get_hdr_len(msg)))
	  return false;
     return true;
}

bool msg_set_hdr(msg_t *msg, const void *hdr, const size_t hdr_len)
{
     g_assert(msg);
     if (msg->hdr)
	  return false;
     msg->hdr = hdr;
     msg->hdr_len = hdr_len;
     return true;
}

bool msg_append_hdr(msg_t *msg, const void *hdr, const size_t hdr_len)
{
     void *tmp;
     
     if (!(tmp = g_malloc(msg->hdr_len + hdr_len)))
	  return false;
     
     if (tmp) {
	  /* Copy the old header */
	  memcpy(tmp, msg->hdr, msg->hdr_len);
	  /* Append new headr */
	  memcpy((char *)tmp + msg->hdr_len, hdr, hdr_len);
	  /* Replace old header with new one */
	  if (msg->priv_hdr)
	       g_free(msg->priv_hdr);
	  msg->priv_hdr = tmp;
	  msg->hdr_len += hdr_len;
	  msg->hdr = msg->priv_hdr;
     }
     return true;
}

bool msg_prepend_hdr(msg_t *msg, const void *hdr, const size_t hdr_len)
{
     void *tmp;

     if (!(tmp = g_malloc(msg->hdr_len + hdr_len)))
	  return false;
     
     if (tmp) {
	  /* Add new header to the head of new buffer */
	  memcpy(tmp, hdr, hdr_len);
	  /* Append old headr */
	  memcpy((char *)tmp + hdr_len, msg->hdr, msg->hdr_len);
	  /* Replace old header with new one */
	  if (msg->priv_hdr)
	       g_free(msg->priv_hdr);
	  msg->priv_hdr = tmp;
	  msg->hdr_len += hdr_len;
	  msg->hdr = msg->priv_hdr;
     }
     return true;
}

size_t msg_get_hdr_len(const msg_t *msg)
{
     g_assert(msg);
     return msg->hdr_len;
}

const void *msg_get_hdr(const msg_t *msg)
{
     g_assert(msg);
     return msg->hdr;
}


bool msg_set_payload(msg_t *msg, const void *payload, const size_t payload_len)
{
     g_assert(msg->refcnt == 1);
     if (msg->payload)
	  return false;
     msg->payload = payload;
     msg->payload_len = payload_len;     
     g_assert(msg->refcnt == 1);
     return true;
}

size_t msg_get_payload_len(const msg_t *msg)
{
     g_assert(msg);
     return msg->payload_len;
}

const void *msg_get_payload(const msg_t *msg)
{
     g_assert(msg);
     return msg->payload;
}

