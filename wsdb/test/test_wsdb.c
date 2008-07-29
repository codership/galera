// Copyright (C) 2007 Codership Oy <info@codership.com>
#include <check.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#include "../src/wsdb_api.h"
#include "wsdb_test.h"
#include "../src/wsdb_priv.h"

//static - compiler does not like it
void wsdb_log_cb(int code, const char *fmt) {
    extern int errno;
    //va_list ap;
    char msg[1024] = {0};
    char FMT[1024] = {0};
    char SYS_ERR[1024] = {0};
    if (errno) {
        sprintf(SYS_ERR, "\nSystem error: %u, %s", errno, strerror(errno));
    }
    sprintf(FMT, "WSDB Error (%u): %s", code, fmt);
    //sprintf(msg, FMT, ap);
    strcat(msg, SYS_ERR);
    fprintf(stderr, msg);
}

#define RAND_ID(table, max) (table[rand() % max])

enum trx_state {
    init,
    query_sent,
    row_sent,
    commit_sent,
};

struct trx_info {
    uint64_t       trx_id;
    uint64_t       conn_id; /* connection identifier */
    int            rows_sent;
    int            queries_sent;
    enum trx_state state;
};

START_TEST (test_wsdb_api)
{
    int i = 0;
    int trx_count = 10;
    int rcode;
    uint64_t trx_seqno = 1;
    char *queries[] = {
        "UPDATE t0 set col1 = hhhh WHERE col2 = bbb",
        "UPDATE t0 set col1 = hhhh WHERE col2 = bbb",
        "UPDATE t0 set col1 = hhhh WHERE col2 = bbb",
        "UPDATE t0 set col1 = hhhh WHERE col2 = bbb"
    };
    char *keys[100];
    struct wsdb_key_part *key_parts[100];
    char actions[] = {
        WSDB_ACTION_DELETE,
        WSDB_ACTION_INSERT,
        WSDB_ACTION_UPDATE
    };
    struct trx_info trxs[trx_count];
    for (i=0; i<trx_count; i++) {
        trxs[i].trx_id       = i;
        trxs[i].conn_id      = i;
        trxs[i].state        = init;
        trxs[i].rows_sent    = 0;
        trxs[i].queries_sent = 0;
    }
    for (i=0; i<100; i++) {
        keys[i] = (char *)malloc(5);
        memset(keys[i], 0, 5);
        sprintf(keys[i], "i:%d", i);
    }
    for (i=0; i<100; i++) {
        key_parts[i] = (struct wsdb_key_part *)malloc(
            sizeof(struct wsdb_key_part)
        );
        key_parts[i]->type   = WSDB_TYPE_CHAR;
        key_parts[i]->length = 5;
        key_parts[i]->data = (void *)malloc(5);
        memset(key_parts[i]->data, 0, 5);
        sprintf(key_parts[i]->data, "i:%d", i);
    }
    /* unit test code */
    rcode = wsdb_init("./data", wsdb_log_cb);
    if (rcode) {
        fail("wsdb init: %d", rcode);
    }

    mark_point();

    for (i=0; i<100; i++) {
        //uint16_t trx_id= RAND_ID(trxs, 10);
        struct trx_info *trx = &RAND_ID(trxs, 10);
        char action = RAND_ID(actions, 3);
        char * query= RAND_ID(queries, 4);
        char *tables[]= {"table0", "table1", "table2", NULL};  
        char *table= RAND_ID(tables, 3);
        //int table_id= RAND_ID(tables, 3);

        mark_point();
        
        switch (trx->state) {
        case init:
        case commit_sent:
#ifdef REMOVED
            rcode = wsdb_append_query(trx->trx_id, query);
            fail_if(rcode, "wsdb_append_query failed: %d", rcode);
            trx->state = query_sent;
            rcode = wsdb_set_local_trx_committed(trx_seqno);
            fail_if(rcode, 
                    "wsdb_set_local_trx_committed failed: %d, %llu, %d", 
                    rcode, trx_seqno, trx->trx_id
            );
            trx->queries_sent++;
            break;
#endif
        case query_sent:
        case row_sent:
            if (trx->rows_sent == 5)  {
                if (trx->queries_sent == 5) {
                     /* TODO: rbr level is not tested here */
                    struct wsdb_write_set *ws = 
                         wsdb_get_write_set(trx->trx_id, trx->conn_id, NULL, 0);
                    fail_if(!ws, "write set read failed");
                    trx->state = commit_sent;
                    rcode = wsdb_delete_local_trx(trx->trx_id);
                    fail_if(rcode, "wsdb_delete_local_trx failed: %d", rcode);
                    rcode = wsdb_append_write_set(trx_seqno++, ws);
                    if(rcode != WSDB_OK && rcode != WSDB_CERTIFICATION_FAIL) {
                        fail("wsdb_append_write_set failed: %d", rcode);
                    }

                    rcode = wsdb_delete_local_trx_info(trx->trx_id);
                    fail_if(rcode, "wsdb_delete_local_trx_info failed: %d", 
                            rcode
                    );

                    trx->queries_sent = 0;
                    trx->rows_sent = 0;
                    trx->trx_id += 10;
                } else {
                    rcode = wsdb_append_query(trx->trx_id, query, time(NULL),
					      rand());
                    fail_if(rcode, "wsdb_append_query failed: %d", rcode);
                    trx->state = query_sent;
                    trx->queries_sent++;
                }
            } else {
                struct wsdb_key_rec key;
                struct wsdb_table_key *table_key;
                //struct wsdb_key_part *key_part;
                table_key = (struct wsdb_table_key *)malloc(
                    sizeof(struct wsdb_table_key)
                );
                table_key->key_parts = RAND_ID(key_parts, 100);
                
                key.dbtable = table;
                key.dbtable_len = strlen(table);
                key.key = table_key;
                table_key->key_part_count = 1;
                //key_part->length = 10;
                //key_part->data = RAND_ID(keys, 100);
                //key_part->type = WSDB_DATA_TYPE_CHAR;
                rcode = wsdb_append_row_key(trx->trx_id, &key, action);
                fail_if(rcode, "wsdb_append_row_key failed: %d", rcode);
                trx->rows_sent++;
                trx->state = row_sent;
            }
            break;
        }
    }
}
END_TEST
#ifdef REMOVED
START_TEST (test_wsdb_dump)
{
    int i = 0;
    int trx_count = 10;
    int rcode;
    uint64_t trx_seqno = 1;

    mark_point();

    dump_trx_files("./data","wsdbtrx");

    mark_point();
}
END_TEST
#endif
START_TEST (test_wsdb_key)
{
    struct wsdb_key_rec wsdb_key;
    struct wsdb_key_rec *wsdb_key2;
    struct file_row_key *file_key;
    struct wsdb_table_key table_key;
    struct wsdb_key_part key_part;

    char *table_name = "test table";
    char *key_data   = "heres the key";

    mark_point();

    wsdb_key.dbtable     = table_name;
    wsdb_key.dbtable_len = strlen(table_name);
    wsdb_key.key         = &table_key;

    table_key.key_part_count = 1;
    table_key.key_parts = &key_part;

    key_part.type = WSDB_TYPE_CHAR;
    key_part.length = strlen(key_data);
    key_part.data = (void *)key_data;

    file_key = wsdb_key_2_file_row_key(&wsdb_key);
    fail_if(!file_key, "wsdb_key_2_file_row_key failed");

    wsdb_key2 = file_row_key_2_wsdb_key(file_key);
    fail_if(!wsdb_key2, "file_row_key_2_wsdb_key failed");

    if (wsdb_key.dbtable_len != wsdb_key2->dbtable_len) {
      fail("dbtable_len differ");
    }
    if (strncmp(wsdb_key.dbtable, wsdb_key2->dbtable, wsdb_key.dbtable_len)) {
      fail("dbtable differ");
    }
    if (wsdb_key.key->key_part_count != wsdb_key2->key->key_part_count) {
      fail("key_part_count differ");
    }
    if (wsdb_key.key->key_parts->type != wsdb_key2->key->key_parts->type) {
      fail("key_part type differ");
    }
    if (wsdb_key.key->key_parts->length != wsdb_key2->key->key_parts->length){
      fail("key_part length differ");
    }
    if (strncmp(wsdb_key.key->key_parts->data,wsdb_key2->key->key_parts->data,
		wsdb_key.key->key_parts->length)){
      fail("key_part data differ");
    }

    mark_point();
}
END_TEST

Suite *make_wsdb_suite(void)
{
    Suite *s = suite_create("wsdb");
    TCase *tc_core = tcase_create("Core");

    suite_add_tcase (s, tc_core);
    tcase_add_test(tc_core, test_wsdb_api);
    tcase_add_test(tc_core, test_wsdb_key);
  
    return s;
}
