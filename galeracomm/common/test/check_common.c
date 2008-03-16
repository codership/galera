


#include <gcomm/addr.h>
#include <gcomm/seq.h>
#include <gcomm/types.h>
#include <gcomm/msg.h>
#include <gcomm/readbuf.h>
#include <gcomm/writebuf.h>
#include <gcomm/protolay.h>

#include <check.h>
#include <stdlib.h>

START_TEST(test_bswaps)
{
     int i;
     size_t buflen = 8;
     char buf[8];
     uint8_t a8, b8;
     uint16_t a16, b16;
     uint32_t a32, b32;
     uint64_t a64, b64;

     for (i = 0; i < 8; i++) {
          a8 = 1 << i;
          fail_unless(write_uint8(a8, buf, buflen, 0) == 1, 
                      "Write uint8");
          fail_unless(read_uint8(buf, buflen, 0, &b8) == 1,
                      "Read uint8");
          fail_unless(a8 == b8, "Convert uint8");
     }

     for (i = 0; i < 16; i++) {
          a16 = 1 << i;
          fail_unless(write_uint16(a16, buf, buflen, 0) == 2, 
                      "Write uint16");
          fail_unless(read_uint16(buf, buflen, 0, &b16) == 2,
                      "Read uint16");
          fail_unless(a16 == b16, "Convert uint16");
     }

     for (i = 0; i < 32; i++) {
          a32 = 1 << i;
          fail_unless(write_uint32(a32, buf, buflen, 0) == 4, 
                      "Write uint32");
          fail_unless(read_uint32(buf, buflen, 0, &b32) == 4,
                      "Read uint32");
          fail_unless(a32 == b32, "Convert uint32 at bit %i", i);
     }

     for (i = 0; i < 64; i++) {
          a64 = 1 << i;
          fail_unless(write_uint64(a64, buf, buflen, 0) == 8, 
                      "Write uint64");
          fail_unless(read_uint64(buf, buflen, 0, &b64) == 8,
                      "Read uint64");
          fail_unless(a64 == b64, "Convert uint64");
     }

}
END_TEST

START_TEST(test_alignment) 
{
     /* Alignment checking is not implemented yet */
}
END_TEST

START_TEST(test_sequential)
{
     int iters;
     int i;
#define B (8 + 16*2 + 32*4 + 64*8)
     size_t buflen = B;
     char buf[B];
#undef B
     size_t offset;
     uint8_t a8, b8;
     uint16_t a16, b16;
     uint32_t a32, b32;
     uint64_t a64, b64;

     for (iters = 0; iters < 64; iters++) {
          offset = 0;
          for (i = 0; i < 8; i++) {
               a8 = 1 << i;
               fail_unless((offset = write_uint8(a8, buf, buflen, offset)), 
                           "Write uint8");
          }

          for (i = 0; i < 16; i++) {
               a16 = 1 << i;
               fail_unless((offset = write_uint16(a16, buf, buflen, offset))
, 
                           "Write uint16");
          }

          for (i = 0; i < 32; i++) {
               a32 = 1 << i;
               fail_unless((offset = write_uint32(a32, buf, buflen, offset))
, 
                           "Write uint32");
          }

          for (i = 0; i < 64; i++) {
               a64 = 1 << i;
               fail_unless((offset = write_uint64(a64, buf, buflen, offset))
, 
                           "Write uint64");
          }

          offset = 0;

          for (i = 0; i < 8; i++) {
               a8 = 1 << i;
               fail_unless((offset = read_uint8(buf, buflen, offset, &b8)),
                           "Read uint8");
               fail_unless(a8 == b8, "Convert uint8 at bit %i %u %u", i, a8, b8)
;
          }


          for (i = 0; i < 16; i++) {
               a16 = 1 << i;
               fail_unless((offset = read_uint16(buf, buflen, offset, &b16))
,
                           "Read uint16");
               fail_unless(a16 == b16, "Convert uint16");
          }


          for (i = 0; i < 32; i++) {
               a32 = 1 << i;
               fail_unless((offset = read_uint32(buf, buflen, offset, &b32))
,
                           "Read uint32");
               fail_unless(a32 == b32, "Convert uint32");
          }


          for (i = 0; i < 64; i++) {
               a64 = 1 << i;
               fail_unless((offset = read_uint64(buf, buflen, offset, &b64))
,
                           "Read uint64");
               fail_unless(a64 == b64, "Convert uint64");
          }
     }


}
END_TEST

START_TEST(test_addr_set)
{
     int n_addr = 32, addr, i;
     addr_t addra, addrb;
     addr_set_t *set, *set2, *set_copy, 
          *set_uni, *set_uni_real, *set_inter, *set_inter_real;
     const addr_set_iter_t *si; 
     char *buf;
     size_t buflen;
     fail_unless(!!(set = addr_set_new()), "Addr set new");
     
     /* Insert addrs in reverse order */
     for (addr = n_addr - 1; addr > -1; addr--) {
          addra = 1 << addr;
          fail_unless(!!addr_set_insert(set, addra), "Set insert %x", addra);
     }
     fail_if(!!addr_set_insert(set, 1), "Set insert 1 again");
     fail_if(!!addr_set_insert(set, 0), "Set insert 0");
     fail_if(!!addr_set_insert(set, ADDR_INVALID), "Set insert %4.4x", ADDR_INVALID);

     addra = 0;
     for (si = addr_set_first(set); si; si = addr_set_next(si)) {
          addrb = addr_cast(si);
          fail_unless(addra < addrb, "Set not ordered");
     }

     for (i = 0; i < 128; i++) {
          addra = 1 << (rand()%32);
          if (addr_set_find(set, addra)) {
               addr_set_erase(set, addra);
               fail_if(!!addr_set_find(set, addra), "Erase fail");
               fail_unless(!!addr_set_insert(set, addra), "Reinsert fail");
          }
     }

     do {
          addra = 1 << (rand()%32);
          if (addr_set_find(set, addra)) {
               addr_set_erase(set, addra);
               fail_if(!!addr_set_find(set, addra), "Erase fail");
          }
     } while (addr_set_size(set) != 0);
     
     for (i = 0; i < 128; i++) {
          addra = rand();
          addr_set_insert(set, addra);
     }

     
     set_copy = addr_set_copy(set);

     fail_unless(addr_set_equal(set, set_copy), "Set copy failed");

     fail_unless(addr_set_is_subset(set_copy, set), "Subset failed");
     
     addr_set_erase(set_copy, addr_cast(addr_set_first(set_copy)));
     fail_if(addr_set_equal(set, set_copy), "Set equal failed");
     fail_unless(addr_set_is_subset(set_copy, set), "Subset failed");
     fail_if(addr_set_is_subset(set, set_copy), "Subset failed");

     addr_set_free(set_copy);
     addr_set_free(set);
     
     set = addr_set_new();
     addr_set_insert(set, 1);
     addr_set_insert(set, 2);
     addr_set_insert(set, 3);
     addr_set_insert(set, 4);
     addr_set_insert(set, 5);
     addr_set_insert(set, 6);

     set2 = addr_set_new();
     addr_set_insert(set2, 4);
     addr_set_insert(set2, 5);
     addr_set_insert(set2, 6);
     addr_set_insert(set2, 7);
     addr_set_insert(set2, 8);

     set_uni_real = addr_set_new();
     addr_set_insert(set_uni_real, 1);
     addr_set_insert(set_uni_real, 2);
     addr_set_insert(set_uni_real, 3);
     addr_set_insert(set_uni_real, 4);
     addr_set_insert(set_uni_real, 5);
     addr_set_insert(set_uni_real, 6);
     addr_set_insert(set_uni_real, 7);
     addr_set_insert(set_uni_real, 8);

     set_uni = addr_set_union(set, set2);
     fail_unless(addr_set_equal(set_uni, set_uni_real), "Set union failed");

     set_inter_real = addr_set_new();
     addr_set_insert(set_inter_real, 4);
     addr_set_insert(set_inter_real, 5);
     addr_set_insert(set_inter_real, 6);

     set_inter = addr_set_intersection(set, set2);
     fail_unless(addr_set_equal(set_inter, set_inter_real), 
                 "Set intersection failed");
     addr_set_free(set);
     addr_set_free(set2);
     addr_set_free(set_uni_real);
     addr_set_free(set_uni);
     addr_set_free(set_inter_real);
     addr_set_free(set_inter);
     set = addr_set_new();
     for (i = 0; i < 32; i++)
          addr_set_insert(set, (addr_t)rand());

     buflen = addr_set_len(set);
     fail_unless(!!(buf = malloc(buflen)), "Malloc fail");
     
     fail_unless(addr_set_write(set, buf, buflen, 0) == buflen, 
                 "Set write failed");
     fail_unless(addr_set_read(buf, buflen, 0, &set_copy) == buflen, 
                 "Set read failed");
     fail_unless(addr_set_equal(set, set_copy), "Set read/write failed");
     
     addr_set_free(set);
     addr_set_free(set_copy);
     free(buf);
}
END_TEST

START_TEST(test_seq_set)
{
     int n_seq = 32, seq, i;
     seq_t seqa, seqb;
     seq_set_t *set, *set2, *set_copy, 
          *set_uni, *set_uni_real, *set_inter, *set_inter_real;
     const seq_set_iter_t *si; 
     char *buf;
     size_t buflen;
     fail_unless(!!(set = seq_set_new()), "Seq set new");
     
     /* Insert seqs in reverse order */
     for (seq = n_seq - 1; seq > -1; seq--) {
          seqa = 1 << seq;
          fail_unless(!!seq_set_insert(set, seqa), "Set insert %x", seqa);
     }
     fail_if(!!seq_set_insert(set, 1), "Set insert 1 again");
     fail_if(!!seq_set_insert(set, SEQ_INVALID), "Set insert %4.4x", SEQ_INVALID);

     seqa = 0;
     for (si = seq_set_first(set); si; si = seq_set_next(si)) {
          seqb = seq_cast(si);
          fail_unless(seqa < seqb, "Set not ordered");
     }

     for (i = 0; i < 128; i++) {
          seqa = 1 << (rand()%32);
          if (seq_set_find(set, seqa)) {
               seq_set_erase(set, seqa);
               fail_if(!!seq_set_find(set, seqa), "Erase fail");
               fail_unless(!!seq_set_insert(set, seqa), "Reinsert fail");
          }
     }

     do {
          seqa = 1 << (rand()%32);
          if (seq_set_find(set, seqa)) {
               seq_set_erase(set, seqa);
               fail_if(!!seq_set_find(set, seqa), "Erase fail");
          }
     } while (seq_set_size(set) != 0);
     
     for (i = 0; i < 128; i++) {
          seqa = rand();
          seq_set_insert(set, seqa);
     }

     
     set_copy = seq_set_copy(set);

     fail_unless(seq_set_equal(set, set_copy), "Set copy failed");

     fail_unless(seq_set_is_subset(set_copy, set), "Subset failed");
     
     seq_set_erase(set_copy, seq_cast(seq_set_first(set_copy)));
     fail_if(seq_set_equal(set, set_copy), "Set equal failed");
     fail_unless(seq_set_is_subset(set_copy, set), "Subset failed");
     fail_if(seq_set_is_subset(set, set_copy), "Subset failed");

     seq_set_free(set_copy);
     seq_set_free(set);
     
     set = seq_set_new();
     seq_set_insert(set, 1);
     seq_set_insert(set, 2);
     seq_set_insert(set, 3);
     seq_set_insert(set, 4);
     seq_set_insert(set, 5);
     seq_set_insert(set, 6);

     set2 = seq_set_new();
     seq_set_insert(set2, 4);
     seq_set_insert(set2, 5);
     seq_set_insert(set2, 6);
     seq_set_insert(set2, 7);
     seq_set_insert(set2, 8);

     set_uni_real = seq_set_new();
     seq_set_insert(set_uni_real, 1);
     seq_set_insert(set_uni_real, 2);
     seq_set_insert(set_uni_real, 3);
     seq_set_insert(set_uni_real, 4);
     seq_set_insert(set_uni_real, 5);
     seq_set_insert(set_uni_real, 6);
     seq_set_insert(set_uni_real, 7);
     seq_set_insert(set_uni_real, 8);

     set_uni = seq_set_union(set, set2);
     fail_unless(seq_set_equal(set_uni, set_uni_real), "Set union failed");

     set_inter_real = seq_set_new();
     seq_set_insert(set_inter_real, 4);
     seq_set_insert(set_inter_real, 5);
     seq_set_insert(set_inter_real, 6);

     set_inter = seq_set_intersection(set, set2);
     fail_unless(seq_set_equal(set_inter, set_inter_real), 
                 "Set intersection failed");
     seq_set_free(set);
     seq_set_free(set2);
     seq_set_free(set_uni_real);
     seq_set_free(set_uni);
     seq_set_free(set_inter_real);
     seq_set_free(set_inter);
     set = seq_set_new();
     for (i = 0; i < 32; i++)
          seq_set_insert(set, (seq_t)rand());

     buflen = seq_set_len(set);
     fail_unless(!!(buf = malloc(buflen)), "Malloc fail");
     
     fail_unless(seq_set_write(set, buf, buflen, 0) == buflen, 
                 "Set write failed");
     fail_unless(seq_set_read(buf, buflen, 0, &set_copy) == buflen, 
                 "Set read failed");
     fail_unless(seq_set_equal(set, set_copy), "Set read/write failed");
     
     seq_set_free(set);
     seq_set_free(set_copy);
     free(buf);
}
END_TEST


START_TEST(test_msg)
{
     int i;
     msg_t *msg1, *msg1_cpy, *msg_foo, *msg_bar, *msg_wtf;
     char *hdr[3] = {"foo", "bar", "wtf?"};
     char *payload = "abcdefg";
     char *buf;
     size_t buflen;
     
     fail_unless(!!(msg1 = msg_new()));
     fail_unless(msg_set_payload(msg1, payload, strlen(payload)));
     for (i = 2; i >= 0; i--)
	  fail_unless(msg_prepend_hdr(msg1, hdr[i], strlen(hdr[i])));
     fail_unless(!!(msg1_cpy = msg_copy(msg1)));
     msg_free(msg1);
     msg1 = NULL;
     
     buflen = msg_get_len(msg1_cpy);
     buf = malloc(buflen);
     memcpy(buf, msg_get_hdr(msg1_cpy), msg_get_hdr_len(msg1_cpy));
     memcpy(buf + msg_get_hdr_len(msg1_cpy), msg_get_payload(msg1_cpy), 
	    msg_get_payload_len(msg1_cpy));
     
     msg_free(msg1_cpy);
     msg1_cpy = NULL;
     
     msg_foo = msg_new();

     msg_set(msg_foo, buf, strlen(hdr[0]), buflen);
     fail_unless(msg_get_len(msg_foo) == buflen);
     
     msg_bar = msg_new();
     msg_set(msg_bar, msg_get_payload(msg_foo), strlen(hdr[1]), 
	     msg_get_payload_len(msg_foo));
     msg_wtf = msg_new();
     msg_set(msg_wtf, msg_get_payload(msg_bar), strlen(hdr[2]), 
	     msg_get_payload_len(msg_bar));
     fail_unless(strncmp(msg_get_hdr(msg_foo), "foo", 3) == 0);
     fail_unless(strncmp(msg_get_hdr(msg_bar), "bar", 3) == 0);
     fail_unless(strncmp(msg_get_hdr(msg_wtf), "wtf?", 4) == 0);
     
     fail_unless(msg_get_payload_len(msg_wtf) == strlen(payload));
     fail_unless(strncmp(msg_get_payload(msg_wtf), payload, strlen(payload)) == 0);
     msg_free(msg_foo);
     msg_free(msg_bar);
     msg_free(msg_wtf);
     free(buf);
}
END_TEST


START_TEST(test_readbuf)
{
    int i;
    char buf[16];
    const char *ptr;
    readbuf_t *rb, *rb_copy;

    for (i = 0; i < 16; i++)
	buf[i] = i;
    
    fail_unless(!!(rb = readbuf_new(buf, 16)));
    fail_unless(!!(ptr = readbuf_get_buf(rb, 0)));
    fail_unless(readbuf_get_buflen(rb) == 16);
    
    for (i = 0; i < 16; i++)
	fail_unless(*(ptr + i) == i);
    fail_unless(!!(rb_copy = readbuf_copy(rb)));
    readbuf_free(rb);
    rb = NULL;
    memset(buf, 0, 16);
    ptr = readbuf_get_buf(rb_copy, 0);
    for (i = 0; i < 16; i++)
	fail_unless(*(ptr + i) == i);    
    readbuf_free(rb_copy);
}
END_TEST

START_TEST(test_writebuf)
{

    int i;
    char buf[16];
    const char *ptr;
    struct hdr1 {
	int a;
	int b;
    } hdr1 = {1, 2};

    struct hdr2 {
	int c;
	int d;
	int e;
    } hdr2 = {3, 4, 5};

    const struct hdrtot {
	int a;
	int b;
	int c;
	int d;
	int e;
    } *tothdr;

    writebuf_t *wb, *wb_copy;

    for (i = 0; i < 16; i++)
	buf[i] = i;

    fail_unless(!!(wb = writebuf_new(buf, 16)));
    fail_unless(!!(ptr = writebuf_get_payload(wb)));
    fail_unless(writebuf_get_payloadlen(wb) == 16);
    
    for (i = 0; i < 16; i++)
	fail_unless(*(ptr + i) == i);
    
    wb_copy = writebuf_copy(wb);
    writebuf_free(wb);
    wb = NULL;
    memset(buf, 0, 16);
    fail_unless(!!(ptr = writebuf_get_payload(wb_copy)));
    for (i = 0; i < 16; i++)
	fail_unless(*(ptr + i) == i);    
    writebuf_free(wb_copy);

    
    fail_unless(!!(wb = writebuf_new(NULL, 0)));

    writebuf_prepend_hdr(wb, &hdr1, sizeof(hdr1));
    writebuf_prepend_hdr(wb, &hdr2, sizeof(hdr2));

    fail_unless(writebuf_get_hdrlen(wb) == 5*4);
    fail_unless(!!(tothdr = writebuf_get_hdr(wb)));
    fail_unless(tothdr->a == 3);
    fail_unless(tothdr->b == 4);
    fail_unless(tothdr->c == 5);
    fail_unless(tothdr->d == 1);
    fail_unless(tothdr->e == 2);
    writebuf_free(wb);

    fail_unless(!!(wb = writebuf_new(NULL, 0)));

    writebuf_prepend_hdr(wb, &hdr2, sizeof(hdr2));
    writebuf_prepend_hdr(wb, &hdr1, sizeof(hdr1));


    fail_unless(writebuf_get_hdrlen(wb) == 5*4);
    fail_unless(!!(tothdr = writebuf_get_hdr(wb)));
    fail_unless(tothdr->a == 1);
    fail_unless(tothdr->b == 2);
    fail_unless(tothdr->c == 3);
    fail_unless(tothdr->d == 4);
    fail_unless(tothdr->e == 5);

    writebuf_rollback_hdr(wb, sizeof(hdr1));
    fail_unless(writebuf_get_hdrlen(wb) == 3*4);
    fail_unless(!!(tothdr = writebuf_get_hdr(wb)));
    fail_unless(tothdr->a == 3);
    fail_unless(tothdr->b == 4);
    fail_unless(tothdr->c == 5);

    writebuf_free(wb);
}
END_TEST

struct proto {
    readbuf_t *rb;
    writebuf_t *wb;
    protolay_t *pl;
    void (*pass_up_cb)(protolay_t *, 
		       const readbuf_t *, const size_t,
		       const up_meta_t *);
    int (*pass_down_cb)(protolay_t *, writebuf_t *, 
			const down_meta_t *);
};

static void to_top(protolay_t *p, const readbuf_t *rb, 
		   const size_t rboff,
		   const up_meta_t *up_meta)
{
    struct proto *priv;
    
    fail_unless(!!(priv = protolay_get_priv(p)));
    priv->rb = readbuf_copy(rb);
}

static void pass_up(protolay_t *p, const readbuf_t *rb, const size_t rboff,
		    const up_meta_t *up_meta)
{
    protolay_pass_up(p, rb, rboff, NULL);
}

static int pass_down(protolay_t *p, writebuf_t *wb, 
		     const down_meta_t *down_meta)
{
    return protolay_pass_down(p, wb, NULL);
}

static int to_bottom(protolay_t *p, writebuf_t *wb,
		     const down_meta_t *down_meta)
{
    /* */
    struct proto *priv;
    
    fail_unless(!!(priv = protolay_get_priv(p)));
    priv->wb = writebuf_copy(wb);
    return 0;
}

START_TEST(test_protolay)
{
    int i;
    struct proto proto[4] = {
	{NULL, NULL, NULL, NULL, &pass_down}, 
	{NULL, NULL, NULL, &to_top, &pass_down}, 
	{NULL, NULL, NULL, &pass_up, &to_bottom}, 
	{NULL, NULL, NULL, &pass_up, NULL}
    };
    
    readbuf_t *rb;
    writebuf_t *wb;
    char buf[16];
    const char *ptr;
    for (i = 0; i < 16; i++)
	buf[i] = i;
    
    for (i = 0; i < 4; i++) {
	fail_unless(!!(proto[i].pl = protolay_new(&proto[i], NULL)));
	fail_unless(&proto[i] == protolay_get_priv(proto[i].pl));
	if (i > 0) {
	    protolay_set_up(proto[i].pl, proto[i - 1].pl, 
			    proto[i].pass_up_cb);
	    protolay_set_down(proto[i - 1].pl, proto[i].pl,
			      proto[i - 1].pass_down_cb);
	}
    }
    
    rb = readbuf_new(buf, 16);
    protolay_pass_up(proto[3].pl, rb, 0, NULL);
    readbuf_free(rb);

    fail_unless(!!(rb = proto[0].rb));
    proto[0].rb = NULL;
    ptr = readbuf_get_buf(rb, 0);
    for (i = 0; i < 16; i++) {
	fail_unless(*(ptr + i) == i);
    }
    readbuf_free(rb);

    
    wb = writebuf_new(NULL, 0);
    protolay_pass_down(proto[0].pl, wb, NULL);
    writebuf_free(wb);
    
    fail_unless(!!(wb = proto[3].wb));
    proto[3].wb = NULL;
    writebuf_free(wb);

    protolay_free(proto[0].pl);

}
END_TEST

static Suite *suite(void)
{
     Suite *s;
     TCase *tc;

     s = suite_create("Common");

     tc = tcase_create("Integers");
     tcase_add_test(tc, test_bswaps);
     tcase_add_test(tc, test_alignment);
     tcase_add_test(tc, test_sequential);
     suite_add_tcase(s, tc);
     
     tc = tcase_create("Address set");
     tcase_add_test(tc, test_addr_set);
     suite_add_tcase(s, tc);
     
     tc = tcase_create("Seq set");
     tcase_add_test(tc, test_seq_set);
     suite_add_tcase(s, tc);

     tc = tcase_create("Msg");
     tcase_add_test(tc, test_msg);
     suite_add_tcase(s, tc);
     
     tc = tcase_create("Readbuf");
     tcase_add_test(tc, test_readbuf);
     suite_add_tcase(s, tc);

     tc = tcase_create("Writebuf");
     tcase_add_test(tc, test_writebuf);
     suite_add_tcase(s, tc);

     tc = tcase_create("Protolay");
     tcase_add_test(tc, test_protolay);
     suite_add_tcase(s, tc);

     return s;
}


int main(void)
{
     int nfail;

     Suite *s; 
     SRunner *sr;

     s = suite();
     sr = srunner_create(s);

     srunner_run_all(sr, CK_NORMAL);
     nfail = srunner_ntests_failed(sr);
     srunner_free(sr);
     return (nfail == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
