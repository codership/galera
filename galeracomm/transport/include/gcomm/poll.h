#ifndef POLL_H
#define POLL_H

#include <gcomm/types.h>

typedef struct poll_ poll_t;

typedef enum {
     POLL_IN = 0x1,
     POLL_OUT = 0x2,
     POLL_ERR = 0x4
} poll_e;

poll_t *poll_new();
void poll_free(poll_t *);

bool poll_insert(poll_t *, int fd, void *,
		 void (*event_cb)(void *, int, poll_e));
void poll_erase(poll_t *, int fd);
void poll_set(poll_t *, int fd, poll_e);
void poll_unset(poll_t *, int fd, poll_e);

int poll_until(poll_t *, int tout);

#endif /* POLL_H */
