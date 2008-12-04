// Copyright (C) 2007 Codership Oy <info@codership.com>
#include <check.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#include "../src/wsdb_api.h"
#include "wsdb_test.h"

//static - compiler does not like it
void xdr_log_cb(int code, const char *fmt) {
    extern int errno;
    char msg[1024] = {0};
    char FMT[1024] = {0};
    char SYS_ERR[1024] = {0};
    if (errno) {
        sprintf(SYS_ERR, "\nSystem error: %u, %s", errno, strerror(errno));
    }
    sprintf(FMT, "WSDB Error (%u): %s", code, fmt);
    strcat(msg, SYS_ERR);
    fprintf(stderr, msg);
}
#define RAND_ID(table, max) (table[rand() % max])

START_TEST (test_xdr)
{
    local_trxid_t trx;
    int rcode;
    int i;
    char *queries[] = {
        "UPDATE t0 set col1 = hhhh WHERE col1 = aaa",
        "UPDATE t1 set col2 = hhhh WHERE col2 = bbb",
        "UPDATE t2 set col3 = hhhh WHERE col3 = ccc",
        "UPDATE t3 set col4 = hhhh WHERE col4 = ddd"
    };
    char *keys[100];
    struct wsdb_key_part *key_parts[100];
    char actions[] = {
        WSDB_ACTION_DELETE,
        WSDB_ACTION_INSERT,
        WSDB_ACTION_UPDATE
    };
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
    rcode = wsdb_init("./data", xdr_log_cb, 0);
    if (rcode) {
        fail("wsdb init: %d", rcode);
    }
    mark_point();

    for (trx=1; trx<10; trx++) {
        int i;
        int max_queries = rand() % 17 + 1;

        XDR xdrs;
        char data[50000];
        int data_len = 50000;
        struct wsdb_write_set *ws_send;
        struct wsdb_write_set  ws_recv;
        
        mark_point();
        
        xdrmem_create(&xdrs, (char *)data, data_len, XDR_ENCODE);

        for (i=0; i<max_queries; i++) {
            char action = RAND_ID(actions, 3);
            char * query= RAND_ID(queries, 4);
            char *tables[]= {"table0", "table1", "table2", NULL};  
            char *table= RAND_ID(tables, 3);
            int max_keys = rand() % 10;
            int k;

            mark_point();
        
            rcode = wsdb_append_query(trx, query, time(NULL), rand());
            fail_if(rcode, "wsdb_append_query failed: %d", rcode);
        
            for (k=0; k < max_keys; k++) {
                struct wsdb_key_rec key;
                struct wsdb_table_key *table_key;
                //struct wsdb_key_part *key_part;
                uint16_t len = (k+1) * 10;          // row data lenght
                char *data   = (char *)malloc(len); // data for row

                table_key = (struct wsdb_table_key *)malloc(
                    sizeof(struct wsdb_table_key)
                );
                table_key->key_parts = RAND_ID(key_parts, 100);
                
                key.dbtable     = table;
                key.dbtable_len = strlen(table);
                key.key         = table_key;
                table_key->key_part_count = 1;
                rcode = wsdb_append_row_key(i, &key, action);
                fail_if(rcode, "wsdb_append_row_key failed: %d", rcode);

                memset(data, 'd', len);
                rcode = wsdb_append_row(i, len, (void *)data);
                fail_if(rcode, "wsdb_append_row failed: %d", rcode);
            }
        }
        /* TODO: rbr level is not tested here */
        ws_send = wsdb_get_write_set(trx, 1, NULL, 0);
        fail_if(!ws_send, "write set read failed");

        if (!xdr_wsdb_write_set(&xdrs, ws_send)) {
            fail("xdr encode failed: %d", rcode);
        }
        mark_point();

        //xdrmem_create(&xdrs, (char *)data, data_len, XDR_DECODE);
        xdrs.x_op = XDR_DECODE;
        xdr_setpos(&xdrs, 0);
        memset(&ws_recv, 0, sizeof(struct wsdb_write_set));
        if (!xdr_wsdb_write_set(&xdrs, &ws_recv)) {
            fail("xdr decode failed: %d", rcode);
        }

        mark_point();
        xdrs.x_op = XDR_FREE;
        if (!xdr_wsdb_write_set(&xdrs, &ws_recv)) {
            fail("xdr free recv failed: %d", rcode);
        }
        mark_point();
    }
    wsdb_close();
}
END_TEST
Suite *make_xdr_suite(void)
{
    Suite *s = suite_create("xdr");
    TCase *tc_core = tcase_create("Core");

    suite_add_tcase (s, tc_core);
    tcase_add_test(tc_core, test_xdr);
  
    return s;
}
