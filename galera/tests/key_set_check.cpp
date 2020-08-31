/* copyright (C) 2013-2020 Codership Oy <info@codership.com>
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
    case KeySet::FLAT16:  ck_abort_msg("FLAT16 is not supported by test");
    case KeySet::FLAT16A: return 16;
    case KeySet::FLAT8:   ck_abort_msg( "FLAT8 is not supported by test");
    case KeySet::FLAT8A:  return 8;
    default:              ck_abort_msg("Unsupported KeySet verison: %d", ver);
    }

    abort();
}

static void test_ver(gu::RecordSet::Version const rsv, int ws_ver)
{
    int const alignment
        (rsv >= gu::RecordSet::VER2 ? gu::RecordSet::VER2_ALIGNMENT : 1);
    KeySet::Version const tk_ver(KeySet::FLAT16A);
    size_t const base_size(version_to_hash_size(tk_ver));

    union { gu::byte_t buf[1024]; gu_word_t align; } reserved;
    assert((uintptr_t(reserved.buf) % GU_WORD_BYTES) == 0);
    TestBaseName const str("key_set_test");
    KeySetOut kso (reserved.buf, sizeof(reserved.buf), str, tk_ver, rsv, ws_ver);

    ck_assert(kso.count() == 0);
    size_t total_size(kso.size());
    log_info << "Start size: " << total_size;

    TestKey tk0(tk_ver, WSREP_KEY_SHARED, false, "a0");
    kso.append(tk0());
    ck_assert(kso.count() == 1);

    total_size += base_size + 2 + 1*4;
    total_size = GU_ALIGN(total_size, alignment);
    ck_assert_msg(total_size == kso.size(), "Size: %zu, expected: %zu",
                  kso.size(), total_size);

    kso.append(tk0());
    ck_assert(kso.count() == 1);

    ck_assert_msg(total_size == kso.size(), "Size: %zu, expected: %zu",
                  kso.size(), total_size);

    TestKey tk1(tk_ver, WSREP_KEY_SHARED, true, "a0", "a1", "a2");
    mark_point();
    kso.append(tk1());
    ck_assert_msg(kso.count() == 3, "key count: expected 3, got %d",
                  kso.count());

    total_size += base_size + 2 + 2*4;
    total_size = GU_ALIGN(total_size, alignment);
    total_size += base_size + 2 + 3*4;
    total_size = GU_ALIGN(total_size, alignment);
    ck_assert_msg(total_size == kso.size(), "Size: %zu, expected: %zu",
                  kso.size(), total_size);

    TestKey tk2(tk_ver, WSREP_KEY_EXCLUSIVE, false, "a0", "a1", "b2");
    kso.append(tk2());
    ck_assert_msg(kso.count() == 4, "key count: expected 4, got %d",
                  kso.count());

    total_size += base_size + 2 + 3*4;
    total_size = GU_ALIGN(total_size, alignment);
    ck_assert_msg(total_size == kso.size(), "Size: %zu, expected: %zu",
                  kso.size(), total_size);

    /* this should update a sronger version of "a2" */
    TestKey tk2_(tk_ver, WSREP_KEY_SEMI, false, "a0", "a1", "a2");
    kso.append(tk2_());
    ck_assert_msg(kso.count() == 5, "key count: expected 5, got %d",
                  kso.count());

    total_size += base_size + 2 + 3*4;
    total_size = GU_ALIGN(total_size, alignment);
    ck_assert_msg(total_size == kso.size(), "Size: %zu, expected: %zu",
                  kso.size(), total_size);

    /* it is a duplicate, but it should add an exclusive verision of the key */
    TestKey tk3(tk_ver, WSREP_KEY_EXCLUSIVE, true, "a0", "a1");
    log_info << "######## Appending exclusive duplicate tk3: begin";
    kso.append(tk3());
    log_info << "######## Appending exclusive duplicate tk3: end";
    ck_assert_msg(kso.count() == 6, "key count: expected 6, got %d",
                  kso.count());

    total_size += base_size + 2 + 2*4;
    total_size = GU_ALIGN(total_size, alignment);
    ck_assert_msg(total_size == kso.size(), "Size: %zu, expected: %zu",
                  kso.size(), total_size);

    /* tk3 should make it impossible to add anything past a0:a1 */
    TestKey tk4(tk_ver, WSREP_KEY_EXCLUSIVE, false, "a0", "a1", "c2");
    log_info << "######## Appending exclusive duplicate tk4: begin";
    kso.append(tk4());
    log_info << "######## Appending exclusive duplicate tk4: end";
    ck_assert_msg(kso.count() == 6, "key count: expected 6, got %d",
                  kso.count());

    ck_assert_msg(total_size == kso.size(), "Size: %zu, expected: %zu",
                  kso.size(), total_size);

    /* adding shared key should have no effect */
    TestKey tk5(tk_ver, WSREP_KEY_SHARED, true, "a0", "a1");
    kso.append(tk5());
    ck_assert_msg(kso.count() == 6, "key count: expected 6, got %d",
                  kso.count());

    ck_assert_msg(total_size == kso.size(), "Size: %zu, expected: %zu",
                  kso.size(), total_size);

    /* adding semi key should have no effect */
    TestKey tk5_(tk_ver, WSREP_KEY_SHARED, true, "a0", "a1");
    kso.append(tk5_());
    ck_assert_msg(kso.count() == 6, "key count: expected 6, got %d",
                  kso.count());

    ck_assert_msg(total_size == kso.size(), "Size: %zu, expected: %zu",
                  kso.size(), total_size);

    /* tk5 should not make any changes */
    TestKey tk6(tk_ver, WSREP_KEY_EXCLUSIVE, false, "a0", "a1", "c2");
    kso.append(tk6());
    ck_assert_msg(kso.count() == 6, "key count: expected 6, got %d",
                  kso.count());

    ck_assert_msg(total_size == kso.size(), "Size: %zu, expected: %zu",
                  kso.size(), total_size);

    /* a0:b1:... should still be possible, should add 2 keys: b1 and c2 */
    TestKey tk7(tk_ver, WSREP_KEY_SEMI, true, "a0", "b1", "c2");
    kso.append(tk7());
    ck_assert_msg(kso.count() == 8, "key count: expected 8, got %d",
                  kso.count());

    total_size += base_size + 2 + 2*4;
    total_size = GU_ALIGN(total_size, alignment);
    total_size += base_size + 2 + 3*4;
    total_size = GU_ALIGN(total_size, alignment);
    ck_assert_msg(total_size == kso.size(), "Size: %zu, expected: %zu",
                  kso.size(), total_size);

    /* make sure a0:b1:b2 is possible despite we have a0:a1:b2 already
     * (should be no collision on b2) */
    TestKey tk8(tk_ver, WSREP_KEY_SEMI, false, "a0", "b1", "b2");
    kso.append(tk8());
    ck_assert_msg(kso.count() == 9, "key count: expected 9, got %d",
                  kso.count());

    total_size += base_size + 2 + 3*4;
    total_size = GU_ALIGN(total_size, alignment);
    ck_assert_msg(total_size == kso.size(), "Size: %zu, expected: %zu",
                  kso.size(), total_size);

    int expected_count(kso.count());
    TestKey tk8_(tk_ver, WSREP_KEY_EXCLUSIVE, false, "a0", "b1", "b2");
    kso.append(tk8_());
    if (3 == ws_ver)
    {
        /* version 3 does not distinguish SEMI and EXCLUSIVE,
           the key should be ignored */
    }
    else if (4 == ws_ver)
    {
        /* in verson 4 EXCLUSIVE is a stronger key than SEMI - should be added
           to the set */

        expected_count++;
        total_size += base_size + 2 + 3*4;
        total_size = GU_ALIGN(total_size, alignment);
    }
    else abort();

    ck_assert_msg(kso.count() == expected_count,"key count: expected %d, got %d",
                  expected_count, kso.count());
    ck_assert_msg(total_size == kso.size(), "Size: %zu, expected: %zu",
                  kso.size(), total_size);

    log_info << "size before huge key: " << total_size;

    char huge_key[2048];
    memset (huge_key, 'x', sizeof(huge_key));
    huge_key[ sizeof(huge_key) - 1 ] = 0;
    TestKey tk9(tk_ver, WSREP_KEY_EXCLUSIVE, false, huge_key, huge_key,huge_key);
    kso.append(tk9());
    expected_count += 3;
    ck_assert_msg(kso.count() == expected_count, "key count: expected %d, got %d",
                  expected_count, kso.count());

    total_size += base_size + 2 + 1*256;
    total_size = GU_ALIGN(total_size, alignment);
    total_size += base_size + 2 + 2*256;
    total_size = GU_ALIGN(total_size, alignment);
    total_size += base_size + 2 + 3*256;
    total_size = GU_ALIGN(total_size, alignment);
    ck_assert_msg(total_size == kso.size(), "Size: %zu, expected: %zu",
                  kso.size(), total_size);

    log_info << "End size: " << kso.size();

    KeySetOut::GatherVector out;
    out->reserve(kso.page_count());
    size_t const out_size(kso.gather(out));

    log_info << "Gather size: " << out_size << ", buf count: " << out->size();
    ck_assert_msg(0 == out_size % alignment, "out size not aligned by %zd",
                  out_size % alignment);

    std::vector<gu::byte_t> in;
    in.reserve(out_size);
    for (size_t i(0); i < out->size(); ++i)
    {
        const gu::byte_t* ptr(reinterpret_cast<const gu::byte_t*>(out[i].ptr));
        in.insert (in.end(), ptr, ptr + out[i].size);
    }

    ck_assert(in.size() == out_size);

    KeySetIn ksi (kso.version(), in.data(), in.size());

    ck_assert_msg(ksi.count() == kso.count(),
                  "Received keys: %d, expected: %d", ksi.count(), kso.count());
    ck_assert_msg(ksi.size() == kso.size(),
                  "Received size: %zu, expected: %zu", ksi.size(), kso.size());

    try
    {
        ksi.checksum();
    }
    catch (std::exception& e)
    {
        ck_abort_msg("%s", e.what());
    }

    int shared(0); // to stiffle clang complaints about unused variables

    int const P_SHARED(KeySet::KeyPart::prefix(WSREP_KEY_SHARED, ws_ver));

    for (int i(0); i < ksi.count(); ++i)
    {
        KeySet::KeyPart kp(ksi.next());
        shared += (kp.prefix() == P_SHARED);
    }

    KeySetIn ksi_empty;

    ck_assert_msg(ksi_empty.count() == 0,
                  "Received keys: %d, expected: %d", ksi_empty.count(), 0);
    ck_assert_msg(ksi_empty.size() == 0,
                  "Received size: %zu, expected: %d", ksi_empty.size(), 0);

    ksi_empty.init (kso.version(), in.data(), in.size());

    ck_assert_msg(ksi_empty.count() == kso.count(),
                  "Received keys: %d, expected: %d",
                  ksi_empty.count(),kso.count());
    ck_assert_msg(ksi_empty.size() == kso.size(),
                  "Received size: %zu, expected: %zu",
                  ksi_empty.size(), kso.size());

    try
    {
        ksi_empty.checksum();
    }
    catch (std::exception& e)
    {
        ck_abort_msg("%s", e.what());
    }

    for (int i(0); i < ksi_empty.count(); ++i)
    {
        KeySet::KeyPart kp(ksi_empty.next());
        shared += (kp.prefix() == P_SHARED);
    }

    ksi_empty.rewind();

    for (int i(0); i < ksi_empty.count(); ++i)
    {
        KeySet::KeyPart kp(ksi_empty.next());
        shared += (kp.prefix() == P_SHARED);
    }

    ck_assert(0 != shared);
}

#ifndef GALERA_ONLY_ALIGNED
START_TEST (ver1_3)
{
    test_ver(gu::RecordSet::VER1, 3);
}
END_TEST
#endif /* GALERA_ONLY_ALIGNED */

START_TEST (ver2_3)
{
    test_ver(gu::RecordSet::VER2, 3);
}
END_TEST

START_TEST (ver2_4)
{
    test_ver(gu::RecordSet::VER2, 4);
}
END_TEST

Suite* key_set_suite ()
{
    TCase* t = tcase_create ("KeySet");
#ifndef GALERA_ONLY_ALIGNED
    tcase_add_test (t, ver1_3);
#endif
    tcase_add_test (t, ver2_3);
    tcase_add_test (t, ver2_4);
    tcase_set_timeout(t, 60);

    Suite* s = suite_create ("KeySet");
    suite_add_tcase (s, t);

    return s;
}
