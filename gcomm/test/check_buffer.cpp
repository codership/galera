
#include "check_gcomm.hpp"

#include "gcomm/writebuf.hpp"
#include "gcomm/readbuf.hpp"

#include <check.h>


using namespace gcomm;


START_TEST(test_readbuf)
{
    const size_t bufsize = 128;
    byte_t buf[bufsize];
    const byte_t *ptr;
    ReadBuf *rb = new ReadBuf(buf, bufsize);

    fail_unless(rb->get_refcnt() == 1);
    for (size_t i = 0; i < bufsize; i++)
    {
        buf[i] = static_cast<byte_t>(i & 0xff);
    }
    ReadBuf *rb_copy = rb->copy();
    fail_unless(rb->get_refcnt() == 2);
    rb->release();
    fail_unless(rb_copy->get_refcnt() == 1);
    memset(buf, 0, bufsize);

    ptr = rb_copy->get_buf(0);
    for (size_t i = 0; i < bufsize; i++)
        fail_unless(ptr[i] == i);

    ReadBuf *rb_trunc = rb_copy->copy(64);

    rb_copy->release();

    ptr = rb_trunc->get_buf(0);
    for (size_t i = 0; i < 64; i++)
        fail_unless(ptr[i] == i + 64);
    rb_trunc->release();


    const byte_t* bufs[3] = {buf, buf + 17, buf + 17 + 45};
    size_t buflens[3] = {17, 45, bufsize - 17 - 45};
    ReadBuf* mrb = new ReadBuf(bufs, buflens, 3, bufsize);
    fail_unless(mrb->get_len() == bufsize);
    fail_unless(memcmp(buf, mrb->get_buf(), bufsize) == 0);
    mrb->release();

}
END_TEST

START_TEST(test_writebuf)
{
    const size_t buflen = 128;
    byte_t buf[buflen];
    const byte_t *ptr;

    for (size_t i = 0; i < buflen; i++)
    {
        buf[i] = static_cast<byte_t>(i & 0xff);
    }
    WriteBuf *wb_copy;
    {
        WriteBuf wb(buf, buflen);
        wb_copy = wb.copy();
    }

    ptr = wb_copy->get_buf();
    for (size_t i = 0; i < buflen; i++)
        fail_unless(ptr[i] == i);
    delete wb_copy;

    WriteBuf *wbp = new WriteBuf(buf, buflen);

    wb_copy = wbp->copy();
    delete wbp;

    memset(buf, 0, buflen);
    ptr = wb_copy->get_buf();
    for (size_t i = 0; i < buflen; i++)
        fail_unless(ptr[i] == i);
    delete wb_copy;


    wbp = new WriteBuf(0, 0);

    byte_t hdr1[3] = {1, 2, 3};
    byte_t hdr2[2] = {4, 5};

    wbp->prepend_hdr(hdr1, 3);
    fail_unless(wbp->get_hdrlen() == 3);
    wbp->prepend_hdr(hdr2, 2);
    fail_unless(wbp->get_hdrlen() == 5);
    ptr = wbp->get_hdr();
    fail_unless(ptr[0] == 4);
    fail_unless(ptr[1] == 5);
    fail_unless(ptr[2] == 1);
    fail_unless(ptr[3] == 2);
    fail_unless(ptr[4] == 3);
    wbp->rollback_hdr(2);
    fail_unless(wbp->get_hdrlen() == 3);
    ptr = wbp->get_hdr();
    fail_unless(ptr[0] == 1);
    fail_unless(ptr[1] == 2);
    fail_unless(ptr[2] == 3);
    delete wbp;

    for (size_t i = 0; i < buflen; i++)
    {
        buf[i] = static_cast<byte_t>(i & 0xff);
    }
    wbp = new WriteBuf(buf, buflen);
    wbp->prepend_hdr(hdr1, 3);

    fail_unless(wbp->get_totlen() == buflen + 3);

    wb_copy = wbp->copy();
    wbp->rollback_hdr(2);

    fail_unless(wbp->get_hdrlen() == 1);
    ptr = wbp->get_hdr();
    fail_unless(ptr[0] == 3);


    fail_unless(wb_copy->get_hdrlen() == 3);
    ptr = wb_copy->get_hdr();
    fail_unless(ptr[0] == 1);
    fail_unless(ptr[1] == 2);
    fail_unless(ptr[2] == 3);

    delete wbp;
    memset(buf, 0, buflen);

    fail_unless(wb_copy->get_len() == buflen);
    ptr = wb_copy->get_buf();
    for (size_t i = 0; i < buflen; i++)
        fail_unless(ptr[i] == i);
    delete wb_copy;

}
END_TEST

START_TEST(test_wb_to_rb)
{

    byte_t buf[64];
    for (size_t i = 0; i < sizeof(buf); ++i)
    {
        buf[i] = static_cast<byte_t>(i & 0xff);
    }

    WriteBuf wb(buf, sizeof(buf));

    wb.prepend_hdr(buf, 16);

    ReadBuf* rb = wb.to_readbuf();
    wb.rollback_hdr(16);

    fail_unless(rb->get_len() == 16 + sizeof(buf));
    
    const byte_t* ptr = rb->get_buf();
    
    for (size_t i = 0; i < 16; ++i)
    {
        fail_unless(ptr[i] == i % 256);
    }
    for (size_t i = 0; i < sizeof(buf); ++i)
    {
        fail_unless(ptr[i + 16] == i % 256);
    }

    rb->release();


    ReadBuf* rb2 = new ReadBuf(buf, sizeof(buf));
    WriteBuf wb2(rb2);
    
    wb2.prepend_hdr(buf, 16);

    fail_unless(wb2.get_totlen() == 16 + sizeof(buf));

    rb = wb2.to_readbuf();
    LOG_INFO(make_int(rb->get_len()).to_string() + make_int(rb2->get_len()).to_string());
    fail_unless(rb->get_len() == 16 + sizeof(buf));

    wb2.rollback_hdr(16);
    rb2->release();

    ptr = rb->get_buf();
    
    for (size_t i = 0; i < 16; ++i)
    {
        fail_unless(ptr[i] == i % 256);
    }
    for (size_t i = 0; i < sizeof(buf); ++i)
    {
        fail_unless(ptr[i + 16] == i % 256);
    }

    rb->release();


}
END_TEST


Suite* buffer_suite()
{
    Suite* s = suite_create("buffer");
    TCase* tc;

    tc = tcase_create("test_readbuf");
    tcase_add_test(tc, test_readbuf);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_writebuf");
    tcase_add_test(tc, test_writebuf);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_wb_to_rb");
    tcase_add_test(tc, test_wb_to_rb);
    suite_add_tcase(s, tc);

    return s;
}
