/* Copyright (C) 2013-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#undef NDEBUG

#include "gu_serialize.hpp"
#include "../src/gu_rset.hpp"

#include "gu_rset_test.hpp"
#include "gu_logger.hpp"
#include "gu_hexdump.hpp"

#include "gu_macros.h"

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
        buf_(static_cast<gu::byte_t*>(::malloc(size_))),
        str_(reinterpret_cast<const char*>(buf_ + sizeof(uint32_t))),
        own_(true)
    {
        ck_assert (size_ <= 0x7fffffff);
        if (0 == buf_) throw std::runtime_error("failed to allocate record");
        gu::byte_t* tmp = const_cast<gu::byte_t*>(buf_);
        gu::serialize4(uint32_t(size_), tmp, 0);
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
        uint32_t ret;
        gu::unserialize4(buf, 0, ret);
        return ret;
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

START_TEST (empty)
{
    gu::RecordSetIn<TestRecord> const rset_in(0, 0);

    ck_assert(0 == rset_in.size());
    ck_assert(0 == rset_in.count());

    try {
        rset_in.checksum();
    }
    catch (std::exception& e)
    {
        ck_abort_msg("%s", e.what());
    }
}
END_TEST

static void
test_version (gu::RecordSet::Version version)
{
    int const alignment(gu::RecordSet::VER2 == version ?
                        gu::RecordSet::VER2_ALIGNMENT : 1);
    size_t const MB = 1 << 20;

    // the choice of sizes below is based on default allocator memory store size
    // of 4MB. If it is changed, these need to be changed too.
    TestRecord rout0(120,  "abc0");
    ck_assert(rout0.serial_size() == 120);
    ck_assert(gtoh32(*reinterpret_cast<const uint32_t*>(rout0.buf())) == 120);
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

    // ensure alignment to 8
    union { gu_word_t align; gu::byte_t buf[1024]; } reserved;
    assert((uintptr_t(reserved.buf) % GU_WORD_BYTES) == 0);
    std::ostringstream os;
    os << "gu_rset_test_ver" << version;
    TestBaseName str(os.str().c_str());
    gu::RecordSetOut<TestRecord> rset_out(reserved.buf, sizeof(reserved), str,
                                          gu::RecordSet::CHECK_MMH64, version);

    size_t offset(rset_out.size());
    ck_assert(1 == rset_out.page_count());

    std::pair<const gu::byte_t*, size_t> rp;
    int rsize;
    const void* rout_ptrs[7];

    // this should be allocated inside current page
    rp = rset_out.append (rout0);
    rout_ptrs[0] = rp.first;
    rsize = rp.second;
    ck_assert(rsize == rout0.serial_size());
    ck_assert(rsize >= 0);
    ck_assert(rsize == TestRecord::serial_size(rp.first, rsize));
    offset += rsize;
    ck_assert(rset_out.size() == offset);

    ck_assert(1 == rset_out.page_count());

    // this should trigger new page since not stored
    rp = rset_out.append (rout1.buf(), rout1.serial_size(), false);
    rout_ptrs[1] = rp.first;
    rsize = rp.second;
    ck_assert(rsize == rout1.serial_size());
    offset += rsize;
    ck_assert(rset_out.size() == offset);

    if (0 == (offset % alignment)) // aligment page may be required
        ck_assert(2 == rset_out.page_count());
    else
        ck_assert(3 == rset_out.page_count());

    // this should trigger new page since previous one was not stored
    rp = rset_out.append (rout2);
    rout_ptrs[2] = rp.first;
    rsize = rp.second;
    ck_assert(rsize == rout2.serial_size());
    ck_assert(rsize >= 0);
    ck_assert(rsize == TestRecord::serial_size(rp.first, rsize));
    offset += rsize;
    ck_assert(rset_out.size() == offset);

    if (0 == (offset % alignment)) // aligment page may be required
        ck_assert_msg(3 == rset_out.page_count(),
                      "Expected %d pages, found %zu", 3, rset_out.page_count());
    else
        ck_assert_msg(4 == rset_out.page_count(),
                      "Expected %d pages, found %zu", 4, rset_out.page_count());

    //***** test partial record appending *****//
    // this should be allocated inside the current page.
    rp = rset_out.append (rout3.buf(), 3);
//    rout_ptrs[2] = rp.first;
    rsize = rp.second;
    offset += rp.second;
    ck_assert((3 + (0 != (offset % alignment))) == rset_out.page_count());

    // this should trigger a new page, since not stored
    rp = rset_out.append (rout3.buf() + 3, rout3.serial_size() - 3, false,
                            false);
    rout_ptrs[3] = rp.first;
    rsize += rp.second;
    ck_assert(rsize == rout3.serial_size());
    offset += rp.second;
    ck_assert(rset_out.size() == offset);

    ck_assert((4 + (0 != (offset % alignment))) == rset_out.page_count());

    // this should trigger new page, because won't fit in the current page
    rp = rset_out.append (rout4);
    rout_ptrs[4] = rp.first;
    rsize = rp.second;
    ck_assert(rsize == rout4.serial_size());
    offset += rsize;
    ck_assert(rset_out.size() == offset);

    ck_assert((5 + (0 != (offset % alignment))) == rset_out.page_count());

    // this should trigger new page, because 4MB RAM limit exceeded
    rp = rset_out.append (rout5);
    rout_ptrs[5] = rp.first;
    rsize = rp.second;
    ck_assert(rsize == rout5.serial_size());
    offset += rsize;
    ck_assert(rset_out.size() == offset);

    if (0 == (offset % alignment)) // aligment page may be required
        ck_assert_msg(6 == rset_out.page_count(),
                      "Expected %d pages, found %zu", 6, rset_out.page_count());
    else
        ck_assert_msg(7 == rset_out.page_count(),
                      "Expected %d pages, found %zu", 7, rset_out.page_count());

    ck_assert(records.size() == size_t(rset_out.count()));

    gu::RecordSet::GatherVector out_bufs;
    out_bufs->reserve (rset_out.page_count());
    bool const padding_page(offset % alignment);

    size_t min_out_size(0);
    for (size_t i = 0; i < records.size(); ++i)
    {
        min_out_size += records[i]->serial_size();
    }

    size_t const out_size (rset_out.gather (out_bufs));

    ck_assert(out_size == rset_out.serial_size());
    ck_assert(out_size > min_out_size && out_size <= offset);
    ck_assert_msg(out_bufs->size() <= size_t(rset_out.page_count()) &&
                  out_bufs->size() >= size_t(rset_out.page_count()-padding_page),
                  "Expected %zu buffers, got: %zd",
                  rset_out.page_count(), out_bufs->size());
    ck_assert((out_size % alignment) == 0); // make sure it is aligned

    /* concatenate all buffers into one */
    std::vector<gu::byte_t> in_buf;
    in_buf.reserve(out_size);
    mark_point();
    for (size_t i = 0; i < out_bufs->size(); ++i)
    {
        // 0th fragment starts with header, so it it can't be used in this check
        // last fragment may be a padding page
        bool const check_fragment(i > 0 && i < (out_bufs->size()- padding_page));

        ck_assert_msg(!check_fragment || rout_ptrs[i] == out_bufs[i].ptr,
                      "Record pointers don't mathch after gather(). "
                      "old: %p, new: %p", rout_ptrs[i],out_bufs[i].ptr);

        ssize_t size;
        int const off(gu::unserialize4(out_bufs[i].ptr, 0, size));

        const char* str =
            reinterpret_cast<const char*>(out_bufs[i].ptr) + off;

        ck_assert_msg(!check_fragment || size > ssize_t(sizeof(uint32_t)),
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
            ck_assert_msg(::strcmp(str, records[k]->c_str()) == 0,
                          "Buffer %zu: appending '%s', expected '%s'",
                          i, str, records[k]->c_str());
        }

        if (i == 1 || i == 4) {
            ck_assert_msg(size == records[k]->serial_size(),
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

        ck_assert(old_size + out_bufs[i].size == in_buf.size());
    }

    ck_assert_msg(in_buf.size() == out_size,
                  "Sent buf size: %zu, recvd buf size: %zu",
                  out_size, in_buf.size());

    log_info << "Resulting RecordSet buffer:\n"
             << gu::Hexdump(in_buf.data(), 32, false) << '\n'
             << gu::Hexdump(in_buf.data(), 32, true);

    gu::RecordSetIn<TestRecord> const rset_in(in_buf.data(), in_buf.size());

    ck_assert(rset_in.size()  == rset_out.size());
    ck_assert(rset_in.count() == rset_out.count());
    ck_assert(rset_in.serial_size() == rset_out.serial_size());

    for (ssize_t i = 0; i < rset_in.count(); ++i)
    {
        TestRecord const rin(rset_in.next());
        ck_assert_msg(rin == *records[i],
                      "Record %zd failed: expected %s, found %s",
                      i, records[i]->c_str(), rin.c_str());
    }

    /* Test checksum method: */
    try {
        rset_in.checksum();
    }
    catch (std::exception& e)
    {
        ck_abort_msg("%s", e.what());
    }

    /* test buf() method */
    gu::RecordSetIn<TestRecord> const rset_in_buf(rset_in.buf().ptr,
                                                  rset_in.buf().size);
    ck_assert(rset_in.count() == rset_in_buf.count());
    ck_assert(rset_in.size()  == rset_in_buf.size());
    ck_assert(rset_in.buf().ptr == rset_in_buf.buf().ptr);
    for (ssize_t i = 0; i < rset_in_buf.count(); ++i)
    {
        TestRecord const rin(rset_in_buf.next());
        ck_assert_msg(rin == *records[i],
                      "Record %zd failed: expected %s, found %s",
                      i, records[i]->c_str(), rin.c_str());
    }

    /* test empty RecordSetIn creation with subsequent initialization */
    gu::RecordSetIn<TestRecord> rset_in_empty;
    ck_assert(rset_in_empty.size()  == 0);
    ck_assert(rset_in_empty.count() == 0);

    try {
        TestRecord const rin(rset_in_empty.next());
        ck_abort_msg("next() succeeded on an empty writeset");
    }
    catch (gu::Exception& e) {
        ck_assert(e.get_errno() == EPERM);
    }

    rset_in_empty.init(in_buf.data(), in_buf.size(), true);
    ck_assert(rset_in_empty.size()  == rset_out.size());
    ck_assert(rset_in_empty.count() == rset_out.count());

    /* Try some data corruption: swap a bit */
    in_buf[10] ^= 1;

    try {
        rset_in.checksum();
        ck_abort_msg("checksum() didn't throw on corrupted set");
    }
    catch (std::exception& e) {}

    try {
        rset_in_empty.checksum();
        ck_abort_msg("checksum() didn't throw on corrupted set");
    }
    catch (std::exception& e) {}
}

START_TEST (ver1)
{
    test_version(gu::RecordSet::VER1);
}
END_TEST

START_TEST (ver2)
{
    test_version(gu::RecordSet::VER2);
}
END_TEST

/* This test is to test how padding mixes with persistent (stored outside)
 * pages. In this case new padding buf needs to be allocated */
static void
test_padding(gu::RecordSet::Version rsv)
{
    int const alignment(gu::RecordSet::VER2 == rsv ?
                        gu::RecordSet::VER2_ALIGNMENT : 1);

    union { gu_word_t align; gu::byte_t buf[1024]; } reserved;
    assert((uintptr_t(reserved.buf) % GU_WORD_BYTES) == 0);
    std::ostringstream os;
    os << "gu_rset_padding_test_ver" << rsv;
    TestBaseName str(os.str().c_str());
    gu::RecordSetOut<uint64_t> rso(reserved.buf, sizeof(reserved), str,
                                   gu::RecordSet::CHECK_MMH64, rsv);

    uint64_t const data_out_volatile(0xaabbccdd);
    uint32_t const data_out_persistent(0xffeeddcc);
    size_t   const payload_size(sizeof(data_out_volatile)
                                + sizeof(data_out_persistent));
    bool     const padding_page(payload_size % alignment);

    {
        uint64_t const d(data_out_volatile);
        rso.append(&d, sizeof(d), true, false);
    }

    rso.append(&data_out_persistent, sizeof(data_out_persistent), false, false);

    gu::RecordSet::GatherVector out;
    size_t const out_size(rso.gather(out));
    /* here we must get a vector of */
    size_t const expected_pages(2 + padding_page);
    ck_assert_msg(out->size() == expected_pages,
                  "Expected %zu pages, got %zu", expected_pages, out->size());

    /* concatenate all out buffers */
    std::vector<gu::byte_t> in_buf;
    in_buf.reserve(out_size);
    for (size_t i(0); i < out->size(); ++i)
    {
        const gu::byte_t* ptr(static_cast<const gu::byte_t*>(out[i].ptr));
        in_buf.insert (in_buf.end(), ptr, ptr + out[i].size);
    }

    ck_assert(in_buf.size() == out_size);

    try
    {
        gu::RecordSetIn<uint64_t> rsi(in_buf.data(), in_buf.size());
        rsi.checksum();
    }
    catch (gu::Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }
}

START_TEST (ver1_padding)
{
    test_padding(gu::RecordSet::VER1);
}
END_TEST

START_TEST (ver2_padding)
{
    test_padding(gu::RecordSet::VER2);
}
END_TEST

/* return the total size of serialized record set
 * @param count number of records
 * @param size  record size */
static size_t
ver2_size(int const count, size_t const size, gu::RecordSet::CheckType ct)
{
    typedef std::vector<gu::byte_t> record_t;
    record_t record(size);
    assert(record.size() == size);

    std::ostringstream os;
    os << "gu_rset_test_ver2_count" << count << "_size" << size;
    TestBaseName name(os.str().c_str());
    gu::RecordSetOut<record_t> rset(NULL, 0, name, ct, gu::RecordSet::VER2);
    for (int i(0); i < count; ++i)
    {
        rset.append(record.data(), record.size());
    }

    gu::RecordSet::GatherVector out_bufs;
    out_bufs->reserve(rset.page_count());

    size_t const out_size(rset.gather(out_bufs));
    ck_assert_msg(0 == (out_size % gu::RecordSet::VER2_ALIGNMENT),
                  "Final size %zu is not multiple of %d. "
                  "Params: count: %d, size: %zu, ct: %d",
                  out_size, gu::RecordSet::VER2_ALIGNMENT, count, size, ct);

    return out_size;
}

START_TEST (ver2_sizes)
{
    gu::RecordSet::CheckType ct;
    size_t s, ct_s, rs, es;
    int c;

#ifdef NDEBUG
    ct   = gu::RecordSet::CHECK_MMH32;
    ct_s = gu::RecordSet::check_size(ct);
    try
    {
        s = ver2_size(128, 1, ct);
        ck_abort_msg("Must throw exception!");
    }
    catch(gu::Exception& e) {}
#endif

    c    = 1024; // max count allowed in "short" header
    s    = 1;    // record size
    ct   = gu::RecordSet::CHECK_NONE;
    ct_s = gu::RecordSet::check_size(ct);
    rs   = ver2_size(c, s, ct);
    // expected size: short header(8) + checksum size + aligned payload size
    es   = 8 + ct_s + GU_ALIGN(c*s, gu::RecordSet::VER2_ALIGNMENT);
    ck_assert(rs == es);

    c   += 1; // this count must force "long" header
    s    = 1; // record size
    ct   = gu::RecordSet::CHECK_MMH64;
    ct_s = gu::RecordSet::check_size(ct);
    rs   = ver2_size(c, s, ct);
    // expected size: long header(16) + checksum size + aligned payload size
    es   = 16 + ct_s + GU_ALIGN(c*s, gu::RecordSet::VER2_ALIGNMENT);
    ck_assert(rs == es);

    ct   = gu::RecordSet::CHECK_MMH128;
    ct_s = gu::RecordSet::check_size(ct);
    c  = 1;                    // record count
    s  = (1 << 14) - ct_s - 8; // max record size representable in "short" header
    rs = ver2_size(c, s, ct);
    // expected size: long header(16) + checksum size + aligned payload size
    es   = 8 + ct_s + GU_ALIGN(c*s, gu::RecordSet::VER2_ALIGNMENT);
    ck_assert(rs == es);

    ct   = gu::RecordSet::CHECK_MMH128;
    ct_s = gu::RecordSet::check_size(ct);
    c  = 1; // record count
    s += 1; // must force "long" header
    rs = ver2_size(c, s, ct);
    // expected size: long header(16) + checksum size + aligned payload size
    es   = 16 + ct_s + GU_ALIGN(c*s, gu::RecordSet::VER2_ALIGNMENT);
    ck_assert(rs == es);

    ct   = gu::RecordSet::CHECK_NONE;
    ct_s = gu::RecordSet::check_size(ct);
    c  = 1023;
    s  = 16;
    rs = ver2_size(c, s, ct);
    // expected size: long header(16) + checksum size + aligned payload size
    es   = 8 + ct_s + GU_ALIGN(c*s, gu::RecordSet::VER2_ALIGNMENT);
    ck_assert(rs == es);
}
END_TEST

Suite* gu_rset_suite ()
{
    Suite* s(suite_create("gu::RecordSet"));

    TCase* t(tcase_create("RecordSet v1"));
    tcase_add_test (t, empty);
    tcase_add_test (t, ver1);
    tcase_add_test (t, ver1_padding);
#ifndef GALERA_ONLY_ALIGNED
    suite_add_tcase (s, t);
#endif

    t = tcase_create("RecordSet v2");
    tcase_add_test (t, ver2);
    tcase_add_test (t, ver2_padding);
    tcase_add_test (t, ver2_sizes);
    suite_add_tcase (s, t);
//    tcase_set_timeout(t, 60);

    return s;
}
