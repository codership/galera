#ifndef VS_BACKEND_SHM_H
#define VS_BACKEND_SHM_H

#include "vs_backend.h"

vs_backend_t *vs_backend_shm_new(const char *conf,
				 poll_t *poll,
				 protolay_t *up_ctx,
				 void (*pass_up_cb)(protolay_t *,
						    const readbuf_t *,
						    const size_t,
						    const up_meta_t *));

/**
 * Split existing group randomly... FOR UNIT TESTING ONLY!!!
 */
void vs_backend_shm_split(void);

/**
 * Merge possible partitions created by split... FOR UNIT TESTING ONLY!!!
 */
void vs_backend_shm_merge(void);

#endif /* VS_BACKEND_SHM_H */
