// Copyright (C) 2007 Codership Oy <info@codership.com>
#include <string.h>
#include <check.h>
#include "../src/mempool.h"
#include "../src/wsdb_priv.h"
#include "wsdb_test.h"

START_TEST (test_mempool)
{
    struct elem {
        char a;
        long b;
        double c;
        char d;
    };
    struct elem *elems[500];

    int i = 0;
    /* unit test code */
    struct mempool *pool = mempool_create(sizeof(struct elem), 100, MEMPOOL_STICKY, false, "test allocator");
    if (!pool) {
        fail("mempool create");
    }
    for (i=0; i<500; i++) {
      elems[i] = (struct elem*)mempool_alloc(pool, sizeof(struct elem));

        fail_if(!elems[i], "could not allocate elem: %d", i);
    }
    for (i=0; i<500; i++) {
        int rcode = mempool_free(pool, elems[i]);

        fail_if(rcode, "could not free elem: %d", i);
    }
    fail_if(mempool_close(pool), "mempool close");
}
END_TEST

START_TEST (test_mempool_rand)
{
    struct elem {
        double c;
        char d;
    };
#define MAX_ELEMS 10000
#define ADD_LOOP  50
#define DEL_LOOP  10
    struct elem *elems[MAX_ELEMS];

    int i;

    /* unit test code */
    struct mempool *pool = mempool_create(sizeof(struct elem), 90, MEMPOOL_DYNAMIC, false, "dynamic allocator");
    if (!pool) {
        fail("mempool create");
    }
    for (i=0; i<MAX_ELEMS/ADD_LOOP; i++) {
        int a,d;
        for (a=0; a<ADD_LOOP; a++) {
            int idx = i*ADD_LOOP + a;
            elems[idx] = 
                (struct elem*)mempool_alloc(pool, sizeof(struct elem));

            fail_if(!elems[idx], "could not allocate elem: %d %d", i, a);

            elems[idx]->c = (double)idx;
            elems[idx]->d = 'd';
        }
        /* try to delete max DEL_LOOP elements */
        for (d=0; d<DEL_LOOP; d++) {
            int up    = i*ADD_LOOP + ADD_LOOP;
            int idx   = rand() % up;
            //idx = (int)((double)rand()/((double)(RAND_MAX)+(double)(1)) * up);

            if (elems[idx]) {
                int rcode = mempool_free(pool, elems[idx]);
                fail_if(rcode, "could not free elem: %d", idx);
                elems[idx] = NULL;
            }
        }

    }

    for (i=0; i<MAX_ELEMS; i++) {
        if (elems[i]) {
            fail_if(elems[i]->c != (double)i, 
                    "elem: %d double differs %d", elems[i]->c);
            fail_if(elems[i]->d != 'd', 
                    "elem: %d char differs %c", i, elems[i]->d);
        }
    }

    for (i=0; i<MAX_ELEMS; i++) {
        if (elems[i]) {
            int rcode = mempool_free(pool, elems[i]);
            fail_if(rcode, "could not free elem: %d", i);
        }
    }
    fail_if(mempool_close(pool), "mempool close");
}
END_TEST

Suite *make_mempool_suite(void)
{
  Suite *s = suite_create("mempool");
  TCase *tc_core = tcase_create("Core");

  suite_add_tcase (s, tc_core);
  tcase_add_test(tc_core, test_mempool);
  tcase_add_test(tc_core, test_mempool_rand);
  
  return s;
}
