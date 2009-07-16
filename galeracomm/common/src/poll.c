#include "galeracomm/poll.h"

#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <glib.h>

typedef struct poll_user_ {
     int fd;
     void (*event_cb)(void *, int, poll_e);
     void *user_context;
} poll_user_t;

struct poll_ {
     size_t pfd_size;
     struct pollfd *pfd;
     GHashTable *user;
};

typedef int (*cmp_f)(const void *, const void *);

poll_user_t *poll_user_new(int fd, void *user_context, void (*event_cb)(void *, int, poll_e))
{
     poll_user_t *u;
     
     u = malloc(sizeof(poll_user_t));
     u->fd = fd;
     u->user_context = user_context;
     u->event_cb = event_cb;
     return u;
}

void poll_user_free(poll_user_t *u)
{
     free(u);
}

int poll_user_cmp(const poll_user_t *a, const poll_user_t *b)
{
     if (a->fd < b->fd)
	  return -1;
     if (a->fd > b->fd)
	  return 1;
     return 0;
}

int poll_user_equal(const poll_user_t *a, const poll_user_t *b)
{
     return poll_user_cmp(a, b) ? FALSE : TRUE;
}

unsigned int poll_user_hash(const poll_user_t *a)
{
     return g_int_hash(&a->fd);
}

poll_t *poll_new()
{
     poll_t *p;

     p = malloc(sizeof(poll_t));
     p->pfd_size = 0;
     p->pfd = NULL;
     p->user = g_hash_table_new((GHashFunc)&poll_user_hash, 
				(GEqualFunc)&poll_user_equal);
     return p;
}

void user_free(void *key __attribute__((unused)), void *value, 
	       void *user_data __attribute__((unused)))
{
     poll_user_free(value);
}

void poll_free(poll_t *p)
{
     if (p) {
	  g_hash_table_foreach(p->user, &user_free, NULL);
	  g_hash_table_destroy(p->user);
	  free(p->pfd);
	  free(p);
     }
}

static int poll_pfd_cmp(const struct pollfd *a, const struct pollfd *b)
{
     if (a->fd < b->fd)
	  return -1;
     if (a->fd > b->fd)
	  return 1;
     return 0;
}


bool poll_insert(poll_t *p, int fd, void *user_context,
		 void (*event_cb)(void *, int, poll_e))
{
     poll_user_t *u;
     struct pollfd pfd = {fd, 0, 0};
     
     if (p->pfd && bsearch(&pfd, p->pfd, p->pfd_size, sizeof(struct pollfd),
			   (cmp_f)&poll_pfd_cmp))
	  return false;
     
     u = poll_user_new(fd, user_context, event_cb);
     g_hash_table_insert(p->user, u, u);
     
     p->pfd = realloc(p->pfd, (p->pfd_size + 1)*sizeof(struct pollfd));
     p->pfd_size++;
     
     p->pfd[p->pfd_size - 1] = pfd;
     qsort(p->pfd, p->pfd_size, sizeof(struct pollfd), 
	   (cmp_f)&poll_pfd_cmp);
     return true;
}

void poll_erase(poll_t *p, int fd)
{
     poll_user_t *u;
     poll_user_t ucmp = {fd, NULL, NULL};
     struct pollfd *pfd;
     struct pollfd pfdcmp = {fd, 0, 0};
     u = g_hash_table_lookup(p->user, &ucmp);
     if (u) {
	  g_hash_table_remove(p->user, &ucmp);
	  poll_user_free(u);
	  g_assert(!g_hash_table_lookup(p->user, &ucmp));
     }
     pfd = bsearch(&pfdcmp, p->pfd, p->pfd_size, sizeof(struct pollfd),
		   (cmp_f)&poll_pfd_cmp);
     if (pfd) {
	  memmove(pfd, pfd + 1, 
		  (p->pfd_size - (pfd - p->pfd) - 1)*sizeof(struct pollfd));
	  p->pfd_size--;
     }
     fprintf(stderr, "Poll, erased %i\n", fd);
}

void poll_set(poll_t *p, int fd, poll_e f)
{
     struct pollfd *pfd;
     struct pollfd pfdcmp = {fd, 0, 0};

     pfd = bsearch(&pfdcmp, p->pfd, p->pfd_size, sizeof(struct pollfd),
		   (cmp_f)&poll_pfd_cmp);
     if (pfd) {
	  pfd->events |= (f & POLL_IN) ? POLLIN : 0;
	  pfd->events |= (f & POLL_OUT) ? POLLOUT : 0;
     }
}

void poll_unset(poll_t *p, int fd, poll_e f)
{
     struct pollfd *pfd;
     struct pollfd pfdcmp = {fd, 0, 0};
     
     pfd = bsearch(&pfdcmp, p->pfd, p->pfd_size, sizeof(struct pollfd),
		   (cmp_f)&poll_pfd_cmp);
     if (pfd) {
	  pfd->events &= (f & POLL_IN) ? ~POLLIN : ~0;
	  pfd->events &= (f & POLL_OUT) ? ~POLLOUT : ~0;
     }
}

int poll_until(poll_t *p, int tout)
{
     int i;
     int ret;
     poll_e e;
     poll_user_t *u;
     poll_user_t ucmp = {0, NULL, NULL};

#if 0
     fprintf(stderr, "polling: ");
     for (i = 0; i < p->pfd_size; i++)
	  fprintf(stderr, "%i (%i) ", p->pfd[i].fd, p->pfd[i].events);
     fprintf(stderr, "\n");
#endif 

     ret = poll(p->pfd, p->pfd_size, tout);
     if (ret == -1) {
	  ret = errno;
     } else if (ret > 0) {
	  for (i = 0; ret; i++) {
	       if (p->pfd[i].revents) {
		    e = (p->pfd[i].revents & POLLIN) ? POLL_IN : 0;
		    e |= (p->pfd[i].revents & POLLOUT) ? POLL_OUT : 0;
		    e |= (p->pfd[i].revents & POLLERR) ? POLL_ERR : 0;
		    ucmp.fd = p->pfd[i].fd;
		    u = g_hash_table_lookup(p->user, &ucmp);
		    g_assert(u);
		    u->event_cb(u->user_context, u->fd, e);
		    ret--;
	       }
	  }
     }
     return ret;
}
