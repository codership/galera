
#include "galeracomm/vs_view.h"
#include <glib.h>
#include <stdlib.h>

struct vs_view_ {
     vs_view_e type;
     vs_view_id_t id;
     addr_set_t *addr;
     addr_set_t *joined_addr;
     addr_set_t *left_addr;
     addr_set_t *partitioned_addr;
};

/*******************************************************************
 * View
 *******************************************************************/

vs_view_t *vs_view_new(const vs_view_e type, const vs_view_id_t id)
{
     vs_view_t *v;
     
     v = g_malloc(sizeof(vs_view_t));
     if (v) {
	  v->type = type;
	  v->id = id;
	  v->addr = addr_set_new();
	  v->joined_addr = addr_set_new();
	  v->left_addr = addr_set_new();
	  v->partitioned_addr = addr_set_new();
     }
     return v;
}

void vs_view_free(vs_view_t *v)
{
     if (v) {
	  addr_set_free(v->addr);
	  addr_set_free(v->joined_addr);
	  addr_set_free(v->left_addr);
	  addr_set_free(v->partitioned_addr);
	  g_free(v);
     }
}

vs_view_t *vs_view_copy(const vs_view_t *v)
{
     vs_view_t *ret;
     ret = g_malloc(sizeof(vs_view_t));
     if (ret) {
	  ret->type = v->type;
	  ret->id = v->id;
	  ret->addr = addr_set_copy(v->addr);
	  ret->joined_addr = addr_set_copy(v->joined_addr);
	  ret->left_addr = addr_set_copy(v->left_addr);
	  ret->partitioned_addr = addr_set_copy(v->partitioned_addr);
     }
     
     return ret;
}

bool vs_view_equal(const vs_view_t *a, const vs_view_t *b)
{
     g_assert(a && b);

     if (a->type != b->type)
	  return false;
     if (a->id != b->id)
	  return false;
     if (!addr_set_equal(a->addr, b->addr))
	  return false;
     if (!addr_set_equal(a->joined_addr, b->joined_addr))
	  return false;
     if (!addr_set_equal(a->left_addr, b->left_addr))
	  return false;
     if (!addr_set_equal(a->partitioned_addr, b->partitioned_addr))
	  return false;
     return true;
}

const addr_set_t *vs_view_get_addr(const vs_view_t *v)
{
     return v ? v->addr : NULL;
}

bool vs_view_addr_insert(vs_view_t *v, const addr_t id)
{
     g_assert(v->addr);
     if (addr_set_insert(v->addr, id))
	  return true;
     return false;
}

bool vs_view_joined_addr_insert(vs_view_t *v, const addr_t id)
{
     g_assert(v->joined_addr);
     if (addr_set_insert(v->joined_addr, id))
	  return true;
     return false;
}

bool vs_view_left_addr_insert(vs_view_t *v, const addr_t id)
{
     g_assert(v->left_addr);
     if (addr_set_insert(v->left_addr, id))
	  return true;
     return false;
}


bool vs_view_partitioned_addr_insert(vs_view_t *v, const addr_t id)
{
     g_assert(v->partitioned_addr);
     if (addr_set_insert(v->partitioned_addr, id))
	  return true;
     return false;
}

bool vs_view_find_addr(const vs_view_t *v, const addr_t id)
{
     return !!addr_set_find(v->addr, id);
}

vs_view_e vs_view_get_type(const vs_view_t *v)
{
     return v->type;
}

vs_view_id_t vs_view_get_id(const vs_view_t *v)
{
     return v->id;
}

size_t vs_view_get_size(const vs_view_t *v)
{
     return addr_set_size(v->addr);
}


size_t vs_view_get_len(const vs_view_t *v)
{
     size_t len;

     /* Type + id */
     len = 2 + 4;
     len += addr_set_len(v->addr);
     len += addr_set_len(v->joined_addr);
     len += addr_set_len(v->left_addr);
     len += addr_set_len(v->partitioned_addr);
     return len;
}

size_t vs_view_read(const void *from, const size_t fromlen, 
		    const size_t from_offset, vs_view_t **v)
{
     uint16_t type;
     size_t offset;
     addr_t view_id;
     vs_view_t *ret;

     if (!(offset = read_uint16(from, fromlen, from_offset, &type)))
	  return 0;
     if (!(offset = read_uint32(from, fromlen, offset, &view_id)))
	  return 0;
     if (!(ret = g_malloc(sizeof(vs_view_t))))
	  return 0;
     ret->id = view_id;
     ret->type = type;
     if (!(offset = addr_set_read(from, fromlen, offset, &ret->addr)))
	  goto err;
     if (!(offset = addr_set_read(from, fromlen, offset, &ret->joined_addr)))
	  goto err;
     if (!(offset = addr_set_read(from, fromlen, offset, &ret->left_addr)))
	  goto err;
     if (!(offset = addr_set_read(from, fromlen, offset, &ret->partitioned_addr)))
	  goto err;
     *v = ret;
     return offset;
err:
     vs_view_free(ret);
     return 0;
}

size_t vs_view_write(const vs_view_t *v, void *to, const size_t tolen,
		     const size_t to_offset)
{
     uint16_t type;
     size_t offset;
     
     type = v->type;
     
     if (!(offset = write_uint16(type, to, tolen, to_offset)))
	  return 0;
     if (!(offset = write_uint32(v->id, to, tolen, offset)))
	  return 0;
     if (!(offset = addr_set_write(v->addr, to, tolen, offset)))
	  return 0;
     if (!(offset = addr_set_write(v->joined_addr, to, tolen, offset)))
	  return 0;
     if (!(offset = addr_set_write(v->left_addr, to, tolen, offset)))
	  return 0;
     if (!(offset = addr_set_write(v->partitioned_addr, to, tolen, offset)))
	  return 0;
     return offset;
}
