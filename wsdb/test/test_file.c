// Copyright (C) 2007 Codership Oy <info@codership.com>
#include <string.h>
#include <check.h>
#include "../src/file.h"
#include "../src/wsdb_priv.h"
#include "../src/wsdb_file.h"
#include "wsdb_test.h"

START_TEST (test_file)
{
    int i = 0;
    /* unit test code */
    struct wsdb_file *file = file_create("./data/testfile", 333);
    if (!file) {
        fail("hash push: %d", i);
    }
    for (i=0; i<100; i++) {
        char data[333] = {'a'};
        file_addr_t addr;
        char val[333] = {'b', 'b'};
        
        data[1] = (char)i;
        addr = file->append_block(file, 333, data);
        fail_if(file->read_block(file, addr, 333, (char *)val), "file read");
        fail_unless((val[1] == (char)i), 
            "file read at: %d, addr: %d (%d != %d)", 
            i, addr, (char)i, val[1]
        );
    }
    fail_if(file->close(file), "file close");
}
END_TEST

START_TEST (test_file_short_buffer)
{
    int i = 0;
    /* unit test code */
    struct wsdb_file *file = file_create("./data/testfile", 333);
    if (!file) {
        fail("file create failed");
    }
    for (i=0; i<100; i++) {
        char data[33] = {'a'};
        file_addr_t addr;
        char val[333] = {'b', 'b'};
        
        data[1] = (char)i;
        addr = file->append_block(file, 33, data);
        fail_if(file->read_block(file, addr, 333, (char *)val), "file read");
        fail_unless((val[1] == (char)i), 
            "file read at: %d, addr: %d (%d != %d)", 
            i, addr, (char)i, val[1]
        );
    }
    fail_if(file->close(file), "file close");
}
END_TEST

START_TEST (test_file_random_write)
{
    int i = 0;
    file_addr_t addrs[100];
    
    /* unit test code */
    struct wsdb_file *file = file_create("./data/testfile",1024);
    if (!file) {
        fail("file open failed");
    }
    for (i=0; i<100; i++) {
        char data[1024];
        memset(data, 1024, 'g');        
        data[1] = (char)i;
        addrs[i] = file->append_block(file, 1024, data);
    }
    for (i=0; i<100; i += 7) {
        char val[1024] = {'b', 'b'};
        
        fail_if(file->read_block(
            file, addrs[i], 1024, (char *)val), "file read"
        );
        fail_unless((val[1] == (char)i), 
            "file read at: %d, addr: %d (%d != %d)", 
            i, addrs[i], (char)i, val[1]
        );
    }
    for (i=1; i<100; i += 11) {
        char data[1024] = {'c', 'c'};
        
        memset(data, 1024, 'h');
        data[1] = (char)(i-11);
        fail_if(file->write_block(
            file, addrs[i], 1024, (char *)data), "file write"
        );
    }
    for (i=1; i<100; i += 11) {
        char val[1024] = {'b', 'b'};
        
        fail_if(file->read_block(
            file, addrs[i], 1024, (char *)val), "file read"
        );
        fail_unless((val[1] == (char)(i-11)), 
            "file read at: %d, addr: %d (%d != %d)", 
            i, addrs[i], (char)i, val[1]
        );
    }
    fail_if(file->close(file), "file close");
}
END_TEST

Suite *make_file_suite(void)
{
  Suite *s = suite_create("file");
  TCase *tc_core = tcase_create("Core");

  suite_add_tcase (s, tc_core);
  tcase_add_test(tc_core, test_file);
  tcase_add_test(tc_core, test_file_short_buffer);  
  tcase_add_test(tc_core, test_file_random_write);
  
  return s;
}
