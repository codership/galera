/* Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#undef NDEBUG

#include "../src/gu_rset.hpp"

#include "gu_rset_test.hpp"
#include "gu_logger.hpp"
#include "gu_hexdump.hpp"

class TestBaseName : public gu::Allocator::BaseName
{
    std::string str_;

public:

    TestBaseName(const char* name) : str_(name) {}
    void print(std::ostream& os) const { os << str_; }
};

class TestRecord : public gu::Serializable
{
public:

    TestRecord (size_t size, const char* str) :
        Serializable(),
        size_(size),
        buf_(reinterpret_cast<gu::byte_t*>(::malloc(size_))),
        str_(reinterpret_cast<const char*>(buf_ + sizeof(uint32_t))),
        own_(true)
    {
        fail_if (size_ > 0x7fffffff);
        if (0 == buf_) throw std::runtime_error("failed to allocate record");
        gu::byte_t* tmp = const_cast<gu::byte_t*>(buf_);
        *reinterpret_cast<uint32_t*>(tmp) = htog32(size_);
        ::strncpy (const_cast<char*>(str_), str, size_ - 4);
    }

    TestRecord (const gu::byte_t* const buf, ssize_t const size)
        : Serializable(),
          size_(TestRecord::serial_size(buf, size)),
          buf_(buf),
          str_(reinterpret_cast<const char*>(buf_ + sizeof(uint32_t))),
          own_(false)
    {}

    TestRecord (const TestRecord& t)
    : size_(t.size_), buf_(t.buf_), str_(t.str_), own_(false) {}

    virtual ~TestRecord () { if (own_) free (const_cast<gu::byte_t*>(buf_)); }

    const gu::byte_t* buf() const { return buf_; }

    const char* c_str() const { return str_; }

    ssize_t serial_size() const { return my_serial_size(); }

    static ssize_t serial_size(const gu::byte_t* const buf, ssize_t const size)
    {
        check_buf (buf, size, 1);
        return gtoh32 (*reinterpret_cast<const uint32_t*>(buf));
    }

    bool operator!= (const TestRecord& t) const
    {
        return (::strcmp(str_, t.str_));
    }

    bool operator== (const TestRecord& t) const
    {
        return (!(*this != t));
    }

private:

    size_t const            size_;
    const gu::byte_t* const buf_;
    const char* const       str_;
    bool const              own_;

    ssize_t my_serial_size () const { return size_; };

    ssize_t my_serialize_to (void* buf, ssize_t size) const
    {
        check_buf (buf, size, size_);

        ::memcpy (buf, buf_, size_);

        return size_;
    }

    static void check_buf (const void* const buf,
                           ssize_t const     size,
                           ssize_t           min_size)
    {
        if (gu_unlikely (buf == 0 || size < min_size))
            throw std::length_error("buffer too short");
    }

    TestRecord& operator= (const TestRecord&);
};


START_TEST (ver0)
{
    size_t const MB = 1 << 20;

    // the choice of sizes below is based on default allocator memory store size
    // of 4MB. If it is changed, these need to be changed too.
    TestRecord rout0(120,  "abc0");
    fail_if (rout0.serial_size() != 120);
    fail_if (gtoh32(*reinterpret_cast<const uint32_t*>(rout0.buf())) != 120);
    TestRecord rout1(121,  "abc1");
    TestRecord rout2(122,  "012345");
    TestRecord rout3(123,  "defghij");
    TestRecord rout4(3*MB, "klm");
    TestRecord rout5(1*MB, "qpr");

    std::vector<TestRecord*> records;
    records.push_back (&rout0);
    records.push_back (&rout1);
    records.push_back (&rout2);
    records.push_back (&rout3);
    records.push_back (&rout4);
    records.push_back (&rout5);

    gu::byte_t reserved[1024];
    TestBaseName str("gu_rset_test");
    gu::RecordSetOut<TestRecord> rset_out(reserved, sizeof(reserved), str,
                                          gu::RecordSet::CHECK_MMH64,
                                          gu::RecordSet::VER1);

    size_t offset(rset_out.size());
    fail_if (1 != rset_out.page_count());

    std::pair<const gu::byte_t*, size_t> rp;
    int rsize;
    const void* rout_ptrs[7];

    // this should be allocated inside current page
    rp = rset_out.append (rout0);
    rout_ptrs[0] = rp.first;
    rsize = rp.second;
    fail_if (rsize != rout0.serial_size());
    fail_if (rsize < 0);
    fail_if (rsize != TestRecord::serial_size(rp.first, rsize));
    offset += rsize;
    fail_if (rset_out.size() != offset);

    fail_if (1 != rset_out.page_count());

    // this should trigger new page since not stored
    rp = rset_out.append (rout1.buf(), rout1.serial_size(), false);
    rout_ptrs[1] = rp.first;
    rsize = rp.second;
    fail_if (rsize != rout1.serial_size());
    offset += rsize;
    fail_if (rset_out.size() != offset);

    fail_if (2 != rset_out.page_count());

    // this should trigger new page since previous one was not stored
    rp = rset_out.append (rout2);
    rout_ptrs[2] = rp.first;
    rsize = rp.second;
    fail_if (rsize != rout2.serial_size());
    fail_if (rsize < 0);
    fail_if (rsize != TestRecord::serial_size(rp.first, rsize));
    offset += rsize;
    fail_if (rset_out.size() != offset);

    fail_if (3 != rset_out.page_count(),
             "Expected %d pages, found %zu", 3, rset_out.page_count());

    //***** test partial record appending *****//
    // this should be allocated inside the current page.
    rp = rset_out.append (rout3.buf(), 3);
//    rout_ptrs[2] = rp.first;
    rsize = rp.second;
    offset += rp.second;
    fail_if (3 != rset_out.page_count());

    // this should trigger a new page, since not stored
    rp = rset_out.append (rout3.buf() + 3, rout3.serial_size() - 3, false,
                            false);
    rout_ptrs[3] = rp.first;
    rsize += rp.second;
    fail_if (rsize != rout3.serial_size());
    offset += rp.second;
    fail_if (rset_out.size() != offset);

    fail_if (4 != rset_out.page_count());

    // this should trigger new page, because won't fit in the current page
    rp = rset_out.append (rout4);
    rout_ptrs[4] = rp.first;
    rsize = rp.second;
    fail_if (rsize != rout4.serial_size());
    offset += rsize;
    fail_if (rset_out.size() != offset);

    fail_if (5 != rset_out.page_count());

    // this should trigger new page, because 4MB RAM limit exceeded
    rp = rset_out.append (rout5);
    rout_ptrs[5] = rp.first;
    rsize = rp.second;
    fail_if (rsize != rout5.serial_size());
    offset += rsize;
    fail_if (rset_out.size() != offset);

    fail_if (6 != rset_out.page_count(),
             "Expected %d pages, found %zu", 5, rset_out.page_count());

    fail_if (records.size() != size_t(rset_out.count()));

    gu::RecordSet::GatherVector out_bufs;
    out_bufs->reserve (rset_out.page_count());

    size_t min_out_size(0);
    for (size_t i = 0; i < records.size(); ++i)
    {
        min_out_size += records[i]->serial_size();
    }

    size_t const out_size (rset_out.gather (out_bufs));

    fail_if (out_size <= min_out_size || out_size > offset);
    fail_if (out_bufs->size() != static_cast<size_t>(rset_out.page_count()),
             "Expected %zu buffers, got: %zd",
             rset_out.page_count(), out_bufs->size());

    /* concatenate all buffers into one */
    std::vector<gu::byte_t> in_buf;
    in_buf.reserve(out_size);
    mark_point();
    for (size_t i = 0; i < out_bufs->size(); ++i)
    {
        // 0th fragment starts with header, so it it can't be used in this check
        fail_if (i > 0 && rout_ptrs[i] != out_bufs[i].ptr,
                 "Record pointers don't mathch after gather(). "
                 "old: %p, new: %p", rout_ptrs[i],out_bufs[i].ptr);

        ssize_t size = gtoh32(
            *reinterpret_cast<const uint32_t*>(out_bufs[i].ptr));

        const char* str =
            reinterpret_cast<const char*>(out_bufs[i].ptr) + sizeof(uint32_t);

        // 0th fragment starts with header, so it it can't be used in this check
        fail_if (i > 0 && size <= ssize_t(sizeof(uint32_t)),
                 "Expected size > 4, got %zd(%#010zx). i = %zu, buf = %s",
                 size, size, i, str);

        // the above variables make have sense only on certain pages
        // hence ifs below

        size_t k = i;
        switch (i)
        {
        case 3:
            break; // 4th page is partial 4th record
        case 1:
        case 2:
            fail_if (::strcmp(str, records[k]->c_str()),
                     "Buffer %zu: appending '%s', expected '%s'",
                     i, str, records[k]->c_str());
        }

        if (i == 1 || i == 4) {
            fail_if (size != records[k]->serial_size(),
                     "Buffer %zu: appending size %zd, expected %zd",
                     i, size, records[k]->serial_size());
        }

        log_info << "\nadding buf " << i << ": "
                 << gu::Hexdump(out_bufs[i].ptr,
                                std::min<ssize_t>(out_bufs[i].size, 24), true);

        size_t old_size = in_buf.size();

        const gu::byte_t* const begin
            (reinterpret_cast<const gu::byte_t*>(out_bufs[i].ptr));

        in_buf.insert (in_buf.end(), begin, begin + out_bufs[i].size);

        fail_if (old_size + out_bufs[i].size != in_buf.size());
    }

    fail_if (in_buf.size() != out_size,
             "Sent buf size: %zu, recvd buf size: %zu",
             out_size, in_buf.size());

    log_info << "Resulting RecordSet buffer:\n"
             << gu::Hexdump(in_buf.data(), 32, false) << '\n'
             << gu::Hexdump(in_buf.data(), 32, true);

    gu::RecordSetIn<TestRecord> const rset_in(in_buf.data(), in_buf.size());

    fail_if (rset_in.size()  != rset_out.size());
    fail_if (rset_in.count() != rset_out.count());

    for (ssize_t i = 0; i < rset_in.count(); ++i)
    {
        TestRecord const rin(rset_in.next());
        fail_if (rin != *records[i], "Record %d failed: expected %s, found %s",
                 i, records[i]->c_str(), rin.c_str());
    }

    /* Test checksum method: */
    try {
        rset_in.checksum();
    }
    catch (std::exception& e)
    {
        fail("%s", e.what());
    }

    /* test buf() method */
    gu::RecordSetIn<TestRecord> const rset_in_buf(rset_in.buf().ptr,
                                                  rset_in.buf().size);
    fail_if(rset_in.count() != rset_in_buf.count());
    fail_if(rset_in.size()  != rset_in_buf.size());
    fail_if (rset_in.buf().ptr != rset_in_buf.buf().ptr);
    for (ssize_t i = 0; i < rset_in_buf.count(); ++i)
    {
        TestRecord const rin(rset_in_buf.next());
        fail_if (rin != *records[i], "Record %d failed: expected %s, found %s",
                 i, records[i]->c_str(), rin.c_str());
    }

    /* test empty RecordSetIn creation with subsequent initialization */
    gu::RecordSetIn<TestRecord> rset_in_empty;
    fail_if (rset_in_empty.size()  != 0);
    fail_if (rset_in_empty.count() != 0);

    try {
        TestRecord const rin(rset_in_empty.next());
        fail ("next() succeeded on an empty writeset");
    }
    catch (gu::Exception& e) {
        fail_if (e.get_errno() != EPERM);
    }

    rset_in_empty.init(in_buf.data(), in_buf.size(), true);
    fail_if (rset_in_empty.size()  != rset_out.size());
    fail_if (rset_in_empty.count() != rset_out.count());

    /* Try some data corruption: swap a bit */
    in_buf[10] ^= 1;

    try {
        rset_in.checksum();
        fail("checksum() didn't throw on corrupted set");
    }
    catch (std::exception& e) {}

    try {
        rset_in_empty.checksum();
        fail("checksum() didn't throw on corrupted set");
    }
    catch (std::exception& e) {}
}
END_TEST

START_TEST (empty)
{
    gu::RecordSetIn<TestRecord> const rset_in(0, 0);

    fail_if (0 != rset_in.size());
    fail_if (0 != rset_in.count());

    try {
        rset_in.checksum();
    }
    catch (std::exception& e)
    {
        fail("%s", e.what());
    }
}
END_TEST

Suite* gu_rset_suite ()
{
    TCase* t = tcase_create ("RecordSet");
    tcase_add_test (t, ver0);
    tcase_add_test (t, empty);
    tcase_set_timeout(t, 60);

    Suite* s = suite_create ("gu::RecordSet");
    suite_add_tcase (s, t);

    return s;
}
