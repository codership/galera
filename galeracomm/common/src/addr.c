#include <gcomm/addr.h>

#include <glib.h>

addr_set_t *addr_set_new()
{
     return (addr_set_t *) g_queue_new();
}

void addr_set_free(addr_set_t *set)
{
     if (set)
	  g_queue_free((GQueue *) set);
}


addr_set_t *addr_set_copy(const addr_set_t *set)
{
     return (addr_set_t *) g_queue_copy((GQueue *) set);
}

static int addr_cmp(const void *av, const void *bv)
{
     addr_t a = (addr_t)av;
     addr_t b = (addr_t)bv;
     if (a < b)
	  return -1;
     if (a > b)
	  return 1;
     return 0;
} 

static int addr_data_cmp(const void *av, const void *bv, void *u)
{
    return addr_cmp(av, bv);
}

addr_set_iter_t *addr_set_find(const addr_set_t *set, const addr_t addr)
{
     return (addr_set_iter_t *) 
	  g_queue_find_custom((GQueue *)set, (gpointer)addr, &addr_cmp);
}

const addr_set_iter_t *addr_set_insert(addr_set_t *set, const addr_t addr)
{
     if (addr == 0 || addr == ADDR_INVALID)
	  return NULL;
     if (addr_set_find(set, addr))
	  return NULL;
     g_queue_insert_sorted((GQueue *) set, (gpointer)addr, 
			   &addr_data_cmp, NULL);
     return addr_set_find(set, addr);
}

const addr_set_iter_t *addr_set_first(const addr_set_t *set)
{
     return set ? (const addr_set_iter_t *) g_queue_peek_head_link((GQueue *) set) : NULL;
}

const addr_set_iter_t *addr_set_next(const addr_set_iter_t *i)
{
     return i ? (addr_set_iter_t *) g_list_next((GList *) i) : NULL;
}

const addr_set_iter_t *addr_set_nth(const addr_set_t *set, int n)
{
     return set ? (const addr_set_iter_t *) 
	  g_list_nth(g_queue_peek_head_link((GQueue *) set), n) : NULL;
}

addr_t addr_cast(const addr_set_iter_t *i)
{
     return i ? (addr_t) ((GList *) i)->data : ADDR_INVALID;
}

void addr_set_erase(addr_set_t *set, const addr_t addr)
{
     addr_set_iter_t *i;
     if (set && (i = addr_set_find(set, addr))) {
	  g_queue_delete_link((GQueue *) set, (GList *)i);
     }
}

size_t addr_set_size(const addr_set_t *set)
{
     return set ? g_queue_get_length((GQueue *) set) : 0;
}



bool addr_set_equal(const addr_set_t *a, const addr_set_t *b)
{
     const addr_set_iter_t *i, *j;
     
     for (i = addr_set_first(a), j = addr_set_first(b); i && j; 
	  i = addr_set_next(i), j = addr_set_next(j)) {
	  if (addr_cast(i) != addr_cast(j))
	       return false;
     }
     return !(i || j); 
}

bool addr_set_is_subset(const addr_set_t *a, const addr_set_t *b)
{
     const addr_set_iter_t *i;
     for (i = addr_set_first(a); i; i = addr_set_next(i))
	  if (!addr_set_find(b, addr_cast(i)))
	       return false;
     return true;
}

addr_set_t *addr_set_union(const addr_set_t *a, const addr_set_t *b)
{
     addr_set_t *r;
     const addr_set_iter_t *i;
     if (!(r = addr_set_copy(a)))
	  return NULL;
     for (i = addr_set_first(b); i; i = addr_set_next(i)) {
	  if (!addr_set_find(r, addr_cast(i)) && 
	      !addr_set_insert(r, addr_cast(i))) {
	       addr_set_free(r);
	       return NULL;
	  }
     }
     return r;
}

addr_set_t *addr_set_intersection(const addr_set_t *a, const addr_set_t *b)
{
     addr_set_t *r;
     const addr_set_iter_t *i;
     if (!(r = addr_set_new()))
	  return NULL;
     for (i = addr_set_first(a); i; i = addr_set_next(i)) {
	  if (addr_set_find(b, addr_cast(i)) && 
	      !addr_set_insert(r, addr_cast(i))) {
	       addr_set_free(r);
	       return NULL;
	  }
     }
     return r;
}

size_t addr_set_len(const addr_set_t *set)
{
     return sizeof(uint32_t) + addr_set_size(set) * sizeof(addr_t);
}

size_t addr_set_read(const void *from, const size_t fromlen, 
		      const size_t from_offset, addr_set_t **set)
{
     size_t offset;
     uint32_t n, i, val;
     addr_set_t *ret;
     
     if (!(offset = read_uint32(from, fromlen, from_offset, &n)))
	  return 0;
     if (fromlen - offset < n * sizeof(addr_t))
	  return 0;
     if (!(ret = addr_set_new()))
	  return 0;
     for (i = 0; i < n; i++) {
	  if (!(offset = read_uint32(from, fromlen, offset, &val)))
	       goto err;
	  if (!addr_set_insert(ret, val))
	       goto err;
     }
     *set = ret;
     return offset;
err:
     addr_set_free(ret);
     return 0;
}

size_t addr_set_write(const addr_set_t *set, void *to, const size_t tolen, 
		       const size_t to_offset)
{
     size_t offset;
     const addr_set_iter_t *i;

     if (!(offset = write_uint32(addr_set_size(set), to, 
				 tolen, to_offset)))
	  return 0;
     
     for (i = addr_set_first(set); i; i = addr_set_next(i)) {
	  if (!(offset = write_uint32(addr_cast(i), to, tolen, offset)))
	       return 0;
     }
     return offset;
}
