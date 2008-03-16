#include <gcomm/seq.h>
#include <glib.h>

static seq_t *seq_new(const seq_t s)
{
     seq_t *ret;
     ret = g_malloc(sizeof(seq_t));
     *ret = s;
     return ret;
}

static void seq_free(seq_t *s)
{
     g_free(s);
}


seq_set_t *seq_set_new()
{
     return (seq_set_t *) g_queue_new();
}

void seq_set_free(seq_set_t *set)
{
     GList *i;
     if (set) {
	  for (i = g_queue_peek_head_link((GQueue*)set); i;
	       i = g_list_next(i))
	       seq_free(i->data);
	  g_queue_free((GQueue *) set);
     }
}


seq_set_t *seq_set_copy(const seq_set_t *set)
{
     const seq_set_iter_t *i;
     seq_set_t *ret;
     ret = seq_set_new();
     for (i = seq_set_first(set); i; i = seq_set_next(i)) {
	  if (!seq_set_insert(ret, seq_cast(i))) {
	       seq_set_free(ret);
	       return NULL;
	  }
     }
     return ret;
}

static int seq_cmp(const void *av, const void *bv)
{
     seq_t a;
     seq_t b;
     
     a = *(seq_t *) av;
     b = *(seq_t *) bv;

     if (a < b)
	  return -1;
     if (a > b)
	  return 1;
     return 0;
} 

static int seq_data_cmp(const void *av, const void *bv, void *u)
{
     return seq_cmp(av, bv);
}

const seq_set_iter_t *seq_set_find(const seq_set_t *set, const seq_t seq)
{
     return (const seq_set_iter_t *) 
	  g_queue_find_custom((GQueue *)set, &seq, &seq_cmp);
}

const seq_set_iter_t *seq_set_insert(seq_set_t *set, const seq_t seq)
{
     seq_t *seq_ins;
     if (seq == SEQ_INVALID)
	  return NULL;
     if (seq_set_find(set, seq))
	  return NULL;
     seq_ins = seq_new(seq);
     g_queue_insert_sorted((GQueue *) set, seq_ins, 
			   &seq_data_cmp, NULL);
     return seq_set_find(set, seq);
}

const seq_set_iter_t *seq_set_first(const seq_set_t *set)
{
     return set ? (const seq_set_iter_t *) g_queue_peek_head_link((GQueue *) set) : NULL;
}

const seq_set_iter_t *seq_set_next(const seq_set_iter_t *i)
{
     return i ? (const seq_set_iter_t *) g_list_next((GList *) i) : NULL;
}

seq_t seq_cast(const seq_set_iter_t *i)
{
     return i ? *(seq_t *) ((GList *) i)->data : SEQ_INVALID;
}

void seq_set_erase(seq_set_t *set, const seq_t seq)
{
     seq_t *seq_ptr;
     const seq_set_iter_t *i;
     if (set && (i = seq_set_find(set, seq))) {
	  seq_ptr = ((GList *)i)->data;
	  g_queue_delete_link((GQueue *) set, (GList *)i);
	  seq_free(seq_ptr);
     }
}

size_t seq_set_size(const seq_set_t *set)
{
     return set ? g_queue_get_length((GQueue *) set) : 0;
}

size_t seq_set_len(const seq_set_t *set)
{
     return sizeof(uint32_t) + seq_set_size(set) * sizeof(seq_t);
}

bool seq_set_equal(const seq_set_t *a, const seq_set_t *b)
{
     const seq_set_iter_t *i, *j;
     
     for (i = seq_set_first(a), j = seq_set_first(b); i && j; 
	  i = seq_set_next(i), j = seq_set_next(j)) {
	  if (seq_cast(i) != seq_cast(j))
	       return false;
     }
     return !(i || j); 
}

bool seq_set_is_subset(const seq_set_t *a, const seq_set_t *b)
{
     const seq_set_iter_t *i;
     for (i = seq_set_first(a); i; i = seq_set_next(i))
	  if (!seq_set_find(b, seq_cast(i)))
	       return false;
     return true;
}

seq_set_t *seq_set_union(const seq_set_t *a, const seq_set_t *b)
{
     seq_set_t *r;
     const seq_set_iter_t *i;
     if (!(r = seq_set_copy(a)))
	  return NULL;
     for (i = seq_set_first(b); i; i = seq_set_next(i)) {
	  if (!seq_set_find(r, seq_cast(i)) && 
	      !seq_set_insert(r, seq_cast(i))) {
	       seq_set_free(r);
	       return NULL;
	  }
     }
     return r;
}

seq_set_t *seq_set_intersection(const seq_set_t *a, const seq_set_t *b)
{
     seq_set_t *r;
     const seq_set_iter_t *i;
     if (!(r = seq_set_new()))
	  return NULL;
     for (i = seq_set_first(a); i; i = seq_set_next(i)) {
	  if (seq_set_find(b, seq_cast(i)) && 
	      !seq_set_insert(r, seq_cast(i))) {
	       seq_set_free(r);
	       return NULL;
	  }
     }
     return r;
}

size_t seq_set_read(const void *from, const size_t fromlen, 
		      const size_t from_offset, seq_set_t **set)
{
     size_t offset;
     uint32_t n, i;
     seq_t val;
     seq_set_t *ret;
     
     if (!(offset = read_uint32(from, fromlen, from_offset, &n)))
	  return 0;
     if (fromlen - offset < n * sizeof(seq_t))
	  return 0;
     if (!(ret = seq_set_new()))
	  return 0;
     for (i = 0; i < n; i++) {
	  if (!(offset = read_uint64(from, fromlen, offset, &val)))
	       goto err;
	  if (!seq_set_insert(ret, val))
	       goto err;
     }
     *set = ret;
     return offset;
err:
     seq_set_free(ret);
     return 0;
}

size_t seq_set_write(const seq_set_t *set, void *to, const size_t tolen, 
		       const size_t to_offset)
{
     size_t offset;
     const seq_set_iter_t *i;

     if (!(offset = write_uint32(seq_set_size(set), to, 
				     tolen, to_offset)))
	  return 0;
     
     for (i = seq_set_first(set); i; i = seq_set_next(i)) {
	  if (!(offset = write_uint64(seq_cast(i), to, tolen, offset)))
	       return 0;
     }
     return offset;
}


