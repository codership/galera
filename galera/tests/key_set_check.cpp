/* copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#undef NDEBUG

#include "test_key.hpp"
#include "../src/key_set.hpp"

#include "gu_logger.hpp"
#include "gu_hexdump.hpp"

#include <check.h>

using namespace galera;

class TestBaseName : public gu::Allocator::BaseName
{
    std::string str_;

public:

    TestBaseName(const char* name) : str_(name) {}
    void print(std::ostream& os) const { os << str_; }
};

static size_t version_to_hash_size (KeySet::Version const ver)
{
    switch (ver)
    {
    case KeySet::FLAT16:  fail("FLAT16 is not supported by test");
    case KeySet::FLAT16A: return 16;
    case KeySet::FLAT8:   fail ("FLAT8 is not supported by test");
    case KeySet::FLAT8A:  return 8;
    default:              fail ("Unsupported KeySet verison: %d", ver);
    }

    abort();
}

static void test_ver(gu::RecordSet::Version const rsv)
{
    int const alignment
        (rsv >= gu::RecordSet::VER2 ? gu::RecordSet::VER2_ALIGNMENT : 1);
    KeySet::Version const tk_ver(KeySet::FLAT16A);
    size_t const base_size(version_to_hash_size(tk_ver));

    union { gu::byte_t buf[1024]; uint64_t align; } reserved;
    assert((uintptr_t(reserved.buf) % GU_MIN_ALIGNMENT) == 0);
    TestBaseName const str("key_set_test");
    KeySetOut kso (reserved.buf, sizeof(reserved.buf), str, tk_ver, rsv);

    fail_if (kso.count() != 0);
    size_t total_size(kso.size());
    log_info << "Start size: " << total_size;

    TestKey tk0(tk_ver, SHARED, false, "a0");
    kso.append(tk0());
    fail_if (kso.count() != 1);

    total_size += base_size + 2 + 1*4;
    total_size = GU_ALIGN(total_size, alignment);
    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    kso.append(tk0());
    fail_if (kso.count() != 1);

    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    TestKey tk1(tk_ver, SHARED, true, "a0", "a1", "a2");
    mark_point();
    kso.append(tk1());
    fail_if (kso.count() != 3, "key count: expected 3, got %d", kso.count());

    total_size += base_size + 2 + 2*4;
    total_size = GU_ALIGN(total_size, alignment);
    total_size += base_size + 2 + 3*4;
    total_size = GU_ALIGN(total_size, alignment);
    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    TestKey tk2(tk_ver, EXCLUSIVE, false, "a0", "a1", "b2");
    kso.append(tk2());
    fail_if (kso.count() != 4, "key count: expected 4, got %d", kso.count());

    total_size += base_size + 2 + 3*4;
    total_size = GU_ALIGN(total_size, alignment);
    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    /* it is a duplicate, but it should add an exclusive verision of the key */
    TestKey tk3(tk_ver, EXCLUSIVE, true, "a0", "a1");
    log_info << "######## Appending exclusive duplicate tk3: begin";
    kso.append(tk3());
    log_info << "######## Appending exclusive duplicate tk3: end";
    fail_if (kso.count() != 5, "key count: expected 5, got %d", kso.count());

    total_size += base_size + 2 + 2*4;
    total_size = GU_ALIGN(total_size, alignment);
    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    /* tk3 should make it impossible to add anything past a0:a1 */
    TestKey tk4(tk_ver, EXCLUSIVE, false, "a0", "a1", "c2");
    kso.append(tk4());
    fail_if (kso.count() != 5, "key count: expected 5, got %d", kso.count());

    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    /* adding shared key should have no effect */
    TestKey tk5(tk_ver, SHARED, true, "a0", "a1");
    kso.append(tk5());
    fail_if (kso.count() != 5, "key count: expected 5, got %d", kso.count());

    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    /* tk5 should not make any changes */
    TestKey tk6(tk_ver, EXCLUSIVE, false, "a0", "a1", "c2");
    kso.append(tk6());
    fail_if (kso.count() != 5, "key count: expected 5, got %d", kso.count());

    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    /* a0:b1:... should still be possible, should add 2 keys: b1 and c2 */
    TestKey tk7(tk_ver, EXCLUSIVE, true, "a0", "b1", "c2");
    kso.append(tk7());
    fail_if (kso.count() != 7, "key count: expected 7, got %d", kso.count());

    total_size += base_size + 2 + 2*4;
    total_size = GU_ALIGN(total_size, alignment);
    total_size += base_size + 2 + 3*4;
    total_size = GU_ALIGN(total_size, alignment);
    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    /* make sure a0:b1:b2 is possible despite we have a0:a1:b2 already
     * (should be no collision on b2) */
    TestKey tk8(tk_ver, EXCLUSIVE, false, "a0", "b1", "b2");
    kso.append(tk8());
    fail_if (kso.count() != 8, "key count: expected 8, got %d", kso.count());

    total_size += base_size + 2 + 3*4;
    total_size = GU_ALIGN(total_size, alignment);
    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    log_info << "size before huge key: " << total_size;

    char huge_key[2048];
    memset (huge_key, 'x', sizeof(huge_key));
    huge_key[ sizeof(huge_key) - 1 ] = 0;
    TestKey tk9(tk_ver, EXCLUSIVE, false, huge_key, huge_key, huge_key);
    kso.append(tk9());
    fail_if (kso.count() != 11, "key count: expected 11, got %d", kso.count());

    total_size += base_size + 2 + 1*256;
    total_size = GU_ALIGN(total_size, alignment);
    total_size += base_size + 2 + 2*256;
    total_size = GU_ALIGN(total_size, alignment);
    total_size += base_size + 2 + 3*256;
    total_size = GU_ALIGN(total_size, alignment);
    fail_if (total_size != kso.size(), "Size: %zu, expected: %zu",
             kso.size(), total_size);

    log_info << "End size: " << kso.size();

    KeySetOut::GatherVector out;
    out->reserve(kso.page_count());
    size_t const out_size(kso.gather(out));

    log_info << "Gather size: " << out_size << ", buf count: " << out->size();
    fail_if(out_size % alignment, "out size not aligned by %d",
            out_size % alignment);

    std::vector<gu::byte_t> in;
    in.reserve(out_size);
    for (size_t i(0); i < out->size(); ++i)
    {
        const gu::byte_t* ptr(reinterpret_cast<const gu::byte_t*>(out[i].ptr));
        in.insert (in.end(), ptr, ptr + out[i].size);
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

    int shared(0); // to stiffle clang complaints about unused variables

    for (int i(0); i < ksi.count(); ++i)
    {
        KeySet::KeyPart kp(ksi.next());
        shared += kp.shared();
    }

    KeySetIn ksi_empty;

    fail_if (ksi_empty.count() != 0,
             "Received keys: %zu, expected: %zu", ksi_empty.count(), 0);
    fail_if (ksi_empty.size() != 0,
             "Received size: %zu, expected: %zu", ksi_empty.size(), 0);

    ksi_empty.init (kso.version(), in.data(), in.size());

    fail_if (ksi_empty.count() != kso.count(),
             "Received keys: %zu, expected: %zu", ksi_empty.count(),kso.count());
    fail_if (ksi_empty.size() != kso.size(),
             "Received size: %zu, expected: %zu", ksi_empty.size(), kso.size());

    try
    {
        ksi_empty.checksum();
    }
    catch (std::exception& e)
    {
        fail("%s", e.what());
    }

    for (int i(0); i < ksi_empty.count(); ++i)
    {
        KeySet::KeyPart kp(ksi_empty.next());
        shared += kp.shared();
    }

    ksi_empty.rewind();

    for (int i(0); i < ksi_empty.count(); ++i)
    {
        KeySet::KeyPart kp(ksi_empty.next());
        shared += kp.shared();
    }

    fail_if(0 == shared);
}

START_TEST (ver1)
{
    test_ver(gu::RecordSet::VER1);
}
END_TEST

START_TEST (ver2)
{
    test_ver(gu::RecordSet::VER2);
}
END_TEST

Suite* key_set_suite ()
{
    TCase* t = tcase_create ("KeySet");
    tcase_add_test (t, ver1);
    tcase_add_test (t, ver2);
    tcase_set_timeout(t, 60);

    Suite* s = suite_create ("KeySet");
    suite_add_tcase (s, t);

    return s;
}
