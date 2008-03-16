
#ifndef VS_BACKEND_EVS_H
#define VS_BACKEND_EVS_H

#include "vs_backend.h"

vs_backend_t *vs_backend_evs_new(const char *, void *, 
				 void (*send_cb)(void *, const vs_msg_t *));

#endif /* VS_BACKEND_EVS_H */
