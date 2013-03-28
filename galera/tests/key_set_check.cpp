/* Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * $Id: gu_rset_test.cpp 2957 2013-02-10 17:25:08Z alex $
 */

#undef NDEBUG

#include "../src/key_set.hpp"

#include "gu_logger.hpp"
#include "gu_hexdump.hpp"

#include <check.h>

using namespace galera;

class TestKey
{
public:

    TestKey (int         ver,
             bool        exclusive,
             std::vector<const char*> parts,
             bool        nocopy = false)
        :
        parts_    (),
        ver_      (ver),
        exclusive_(exclusive),
        nocopy_   (nocopy)
    {
        parts_.reserve(parts.size());

        for (size_t i = 0; i < parts.size(); ++i)
        {
            wsrep_buf_t b = { parts[i], parts[i] ? strlen(parts[i]) + 1 : 0 };
            parts_.push_back(b);
        }
    }

    TestKey (int         ver,
             bool        exclusive,
             bool        nocopy,
             const char* part0,
             const char* part1 = 0,
             const char* part2 = 0,
             const char* part3 = 0,
             const char* part4 = 0,
             const char* part5 = 0,
             const char* part6 = 0,
             const char* part7 = 0,
             const char* part8 = 0,
             const char* part9 = 0
        )
        :
        parts_    (),
        ver_      (ver),
        exclusive_(exclusive),
        nocopy_   (nocopy)
    {
        parts_.reserve(10);

        (push_back(part0) &&
         push_back(part1) &&
         push_back(part2) &&
         push_back(part3) &&
         push_back(part4) &&
         push_back(part5) &&
         push_back(part6) &&
         push_back(part7) &&
         push_back(part8) &&
         push_back(part9));

    }

    KeyData
    operator() ()
    {
        return KeyData (ver_, parts_.data(), parts_.size(),
                        nocopy_, !exclusive_);
    }

private:

    std::vector<wsrep_buf_t> parts_;

    int  const ver_;
    bool const exclusive_;
    bool const nocopy_;

    bool
    push_back (const char* const p)
    {
        if (p)
        {
            wsrep_buf_t b = { p, strlen(p) + 1 };
            parts_.push_back(b);
            return true;
        }

        return false;
    }

};

enum
{
    SHARED = false,
    EXCLUSIVE = true
};

static size_t version_to_hash_size (KeySet::Version const ver)
{
    switch (ver)
    {
    case KeySet::FLAT16:  fail("FLAT16 is not supported by test");
    case KeySet::FLAT16A: return 16;
    case KeySet::FLAT8:   fail ("FLAT8 is not supported by test");
    case KeySet::FLAT8A:  return 8;
    default:      fail ("Unsupported KeySet verison: %d", ver);
    }

    abort();
}

START_TEST (ver0)
{
    KeySet::Version const tk_ver(KeySet::FLAT16A);
    size_t const base_size(version_to_hash_size(tk_ver));

    KeySetOut kso ("key_set_test", tk_ver);

    fail_if (kso.count() != 0);
    size_t total_size(kso.size());
    log_info << "Start size: " << total_size;

    TestKey tk0(tk_ver, SHARED, true, "a0");
    kso.append(tk0());
    fail_if (kso.count() != 1);

    total_size += base_size + 2 + 1*4;
    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    kso.append(tk0());
    fail_if (kso.count() != 1);

    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    TestKey tk1(tk_ver, SHARED, false, "a0", "a1", "a2");
    kso.append(tk1());
    fail_if (kso.count() != 3, "key count: expected 3, got %d", kso.count());

    total_size += base_size + 2 + 2*4;
    total_size += base_size + 2 + 3*4;
    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    TestKey tk2(tk_ver, EXCLUSIVE, true, "a0", "a1", "b2");
    kso.append(tk2());
    fail_if (kso.count() != 4, "key count: expected 4, got %d", kso.count());

    total_size += base_size + 2 + 3*4;
    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    /* it is a duplicate, but it should add an exclusive verision of the key */
    TestKey tk3(tk_ver, EXCLUSIVE, false, "a0", "a1");
    log_info << "######## Appending exclusive duplicate tk3: begin";
    kso.append(tk3());
    log_info << "######## Appending exclusive duplicate tk3: end";
    fail_if (kso.count() != 5, "key count: expected 5, got %d", kso.count());

    total_size += base_size + 2 + 2*4;
    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    /* tk3 should make it impossible to add anything past a0:a1 */
    TestKey tk4(tk_ver, EXCLUSIVE, true, "a0", "a1", "c2");
    kso.append(tk4());
    fail_if (kso.count() != 5, "key count: expected 5, got %d", kso.count());

    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    /* adding shared key should have no effect */
    TestKey tk5(tk_ver, SHARED, false, "a0", "a1");
    kso.append(tk5());
    fail_if (kso.count() != 5, "key count: expected 5, got %d", kso.count());

    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    /* tk5 should not make any changes */
    TestKey tk6(tk_ver, EXCLUSIVE, true, "a0", "a1", "c2");
    kso.append(tk6());
    fail_if (kso.count() != 5, "key count: expected 5, got %d", kso.count());

    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    /* a0:b1:... should still be possible, should add 2 keys: b1 and c2 */
    TestKey tk7(tk_ver, EXCLUSIVE, false, "a0", "b1", "c2");
    kso.append(tk7());
    fail_if (kso.count() != 7, "key count: expected 7, got %d", kso.count());

    total_size += base_size + 2 + 2*4;
    total_size += base_size + 2 + 3*4;
    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    /* make sure a0:b1:b2 is possible despite we have a0:a1:b2 already
     * (should be no collision on b2) */
    TestKey tk8(tk_ver, EXCLUSIVE, true, "a0", "b1", "b2");
    kso.append(tk8());
    fail_if (kso.count() != 8, "key count: expected 8, got %d", kso.count());

    total_size += base_size + 2 + 3*4;
    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    log_info << "size before huge key: " << total_size;

    char huge_key[2048];
    memset (huge_key, 'x', sizeof(huge_key));
    huge_key[ sizeof(huge_key) - 1 ] = 0;
    TestKey tk9(tk_ver, EXCLUSIVE, true, huge_key, huge_key, huge_key);
    kso.append(tk9());
    fail_if (kso.count() != 11, "key count: expected 11, got %d", kso.count());

    total_size += base_size + 2 + 1*256;
    total_size += base_size + 2 + 2*256;
    total_size += base_size + 2 + 3*256;
    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    log_info << "End size: " << kso.size();

    std::vector<gu::Buf> out;
    out.reserve(kso.page_count());
    size_t const out_size(kso.gather(out));

    log_info << "Gather size: " << out_size << ", buf count: " << out.size();

    std::vector<gu::byte_t> in;
    in.reserve(out_size);
    for (size_t i(0); i < out.size(); ++i)
    {
        in.insert (in.end(), out[i].ptr, out[i].ptr + out[i].size);
    }

    fail_if (in.size() != out_size);

    KeySetIn ksi (kso.version(), in.data(), in.size());

    fail_if (ksi.count() != kso.count(),
             "Received keys: %zu, expected: %zu", ksi.count(), kso.count());
    fail_if (ksi.size() != kso.size(),
             "Received size: %zu, expected: %zu", ksi.size(), kso.size());

    try
    {
        ksi.checksum();
    }
    catch (std::exception& e)
    {
        fail("%s", e.what());
    }

    for (int i(0); i < ksi.count(); ++i)
    {
        KeySet::KeyPart kp(ksi.next());
    }
}
END_TEST

Suite* key_set_suite ()
{
    TCase* t = tcase_create ("KeySet");
    tcase_add_test (t, ver0);
    tcase_set_timeout(t, 60);

    Suite* s = suite_create ("KeySet");
    suite_add_tcase (s, t);

    return s;
}
