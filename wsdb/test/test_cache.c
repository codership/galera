// Copyright (C) 2007 Codership Oy <info@codership.com>
#include <check.h>
#include <string.h>

#include "../src/file_cache.h"
#include "../src/file.h"
#include "../src/wsdb_priv.h"
#include "../src/wsdb_file.h"
#include "wsdb_test.h"

START_TEST (test_cache)
{
    int i = 0;
    uint16_t block_size = 777;
    cache_id_t cids[300];

    /* unit test code */
    struct wsdb_file *file = file_create("./test_cached_file", block_size);
    struct file_cache *cache = file_cache_open(file, 512000, 100);
    
    if (!file) {
        fail("cached file create: %d");
    }
    if (!cache) {
        fail("cache create: %d");
    }
    for (i=0; i<300; i++) {
        char *data;
        cache_id_t cache_id = file_cache_allocate_id(cache);

        cids[i] = cache_id;

        data = (char *)file_cache_new(cache, cache_id);
        *(int *)(data) = i;
        memset(data + sizeof(int), 'a', block_size - sizeof(int));        
    }
    for (i=0; i<300; i++) {
        char *data;

        data = file_cache_get(cache, cids[i]);
	fail_unless(!!data, "no data for id %d", cids[i]);
        fail_if(*(int*)(data) != i, "bad i: %d != %d", i, *(int*)(data));
    }
    fail_if(file_cache_close(cache), "cache close");
    fail_if(file->close(file), "file close");
}
END_TEST

START_TEST (test_cache_overflow)
{
    int i = 0;
    uint16_t block_size = 1000;
    cache_id_t cids[130];

    /* 50 blocks should fit in this cache */
    struct wsdb_file *file = file_create("./test_cached_file", block_size);
    struct file_cache *cache = file_cache_open(file, 10000, 60);
    
    if (!file) {
        fail("cached file create: %d");
    }
    if (!cache) {
        fail("cache create: %d");
    }

    /* ~80 blocks should overflow */
    for (i=0; i<80; i++) {
        char *data;
        cache_id_t cache_id = file_cache_allocate_id(cache);

        cids[i] = cache_id;

        data = (char *)file_cache_new(cache, cache_id);
        *(int *)(data) = i;
        memset(data + sizeof(int), 'b', block_size - sizeof(int));

        if (i%3 == 0) {
            int rcode = file_cache_forget(cache, cache_id);
            fail_if(rcode, "cache forget: %d", rcode);
        }
    }
    mark_point();

    for (i=0; i<80; i++) {
        char *data;

        data = file_cache_get(cache, cids[i]);
	fail_unless(!!data, "no data for id %d", cids[i]);
        fail_if((int)(*data) != i, "bad i: %d != %d", i, *(int*)(data));
        if (i%2 == 0) {
            int rcode = file_cache_forget(cache, cids[i]);
            fail_if(rcode, "cache forget: %d", rcode);
        }
    }
    mark_point();
    for (i=10; i<70; i += 3) {
        int rcode = file_cache_forget(cache, cids[i]);
        fail_if(rcode, "cache forget: %d", rcode);
    }
    mark_point();
    for (i=80; i<130; i++) {
        char *data;
        cache_id_t cache_id = file_cache_allocate_id(cache);

        cids[i] = cache_id;

        data = (char *)file_cache_new(cache, cache_id);
        *(int *)(data) = i;
        memset(data + sizeof(int), 'b', block_size - sizeof(int));        
        if (i%2 == 0) {
            int rcode = file_cache_forget(cache, cache_id);
            fail_if(rcode, "cache forget: %d", rcode);
        }
    }
    mark_point();

    for (i=21; i<130; i +=11) {
        int rcode = file_cache_delete(cache, cids[i]);
        fail_if(rcode, "cache delete: %d", rcode);
    }

    fail_if(file_cache_close(cache), "cache close");
    fail_if(file->close(file), "file close");
}
END_TEST

Suite *make_cache_suite(void)
{
  Suite *s = suite_create("cache");
  TCase *tc_core = tcase_create("Core");

  suite_add_tcase (s, tc_core);
  tcase_add_test(tc_core, test_cache);
  tcase_add_test(tc_core, test_cache_overflow);
  
  return s;
}
