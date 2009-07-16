#ifndef VS_VIEW_H
#define VS_VIEW_H

#include <gcomm/addr.h>

typedef enum {
     VS_VIEW_NONE,
     VS_VIEW_TRANS,
     VS_VIEW_REG
} vs_view_e;

typedef uint32_t vs_view_id_t;
#define VS_VIEW_INVALID (vs_view_id_t)-1


typedef struct vs_view_ vs_view_t;

/**
 * VS view
 */

vs_view_t *vs_view_new(const vs_view_e, const vs_view_id_t);
void vs_view_free(vs_view_t *);
vs_view_t *vs_view_copy(const vs_view_t *);

/*
 * 
 */
bool vs_view_find_addr(const vs_view_t *, const addr_t);
bool vs_view_equal(const vs_view_t *a, const vs_view_t *b);

/*
 * Insert addresses
 */
bool vs_view_addr_insert(vs_view_t *v, const addr_t addr);
bool vs_view_joined_addr_insert(vs_view_t *v, const addr_t addr);
bool vs_view_left_addr_insert(vs_view_t *v, const addr_t addr);
bool vs_view_partitioned_addr_insert(vs_view_t *v, const addr_t addr);

/*
 * Getters 
 */
const addr_set_t *vs_view_get_addr(const vs_view_t *);
const addr_set_t *vs_view_get_joined_addr(const vs_view_t *);
const addr_set_t *vs_view_get_left_addr(const vs_view_t *);
const addr_set_t *vs_view_get_partitioned_addr(const vs_view_t *);

vs_view_e vs_view_get_type(const vs_view_t *);
vs_view_id_t vs_view_get_id(const vs_view_t *);
size_t vs_view_get_size(const vs_view_t *);

/*
 * View serialization
 */
size_t vs_view_get_len(const vs_view_t *);
size_t vs_view_read(const void *, const size_t, const size_t, vs_view_t **);
size_t vs_view_write(const vs_view_t *, void *, const size_t, const size_t);

#endif /* VS_VIEW_H */
