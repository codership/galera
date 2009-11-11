/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */


#include "check_gcomm.hpp"

#include "gcomm/view.hpp"
#include "gcomm/types.hpp"
#include "gcomm/map.hpp"

#include <utility>
#include <iostream>
#include <stdexcept>

#include <galerautils.hpp>

using std::pair;
using std::make_pair;
using std::string;

#include "check_templ.hpp"

#include <check.h>

using namespace gu;
using namespace gu::net;

using namespace gcomm;


START_TEST(test_sizes)
{
    uint8_t u8(3);
    fail_unless(make_int(u8).serial_size() == 1);

    uint16_t u16(3);
    fail_unless(make_int(u16).serial_size() == 2);

    uint32_t u32(3);
    fail_unless(make_int(u32).serial_size() == 4);

    uint64_t u64(3);
    fail_unless(make_int(u64).serial_size() == 8);
    


    String<16> str16("fubar");
    fail_unless(str16.serial_size() == 16);


    check_serialization(str16, 16, String<16>());


}
END_TEST



START_TEST(test_serialization)
{
    check_serialization(make_int<uint8_t>(0xab), 1, make_int<uint8_t>(0));
    check_serialization(make_int<uint16_t>(0xabab), 2, make_int<uint16_t>(0));
    check_serialization(make_int<uint32_t>(0xabababab), 4, make_int<uint32_t>(0));
    check_serialization(make_int<uint64_t>(0xababababababababLLU), 8, make_int<uint64_t>(0));
}
END_TEST

START_TEST(test_uuid)
{
    UUID uuid;
    fail_unless(uuid.to_string() == "00000000-0000-0000-0000-000000000000");
    for (size_t i = 0; i < 159; ++i)
    {
        UUID uuidrnd(0, 0);
        log_debug << uuidrnd;
    }

    UUID uuid1(0, 0);
    UUID uuid2(0, 0);

    fail_unless(uuid1 < uuid2);

}
END_TEST


START_TEST(test_view)
{
    ViewId v1(V_REG,   UUID(1), 1);
    ViewId v2(V_TRANS, UUID(1), 1);
    ViewId v3(V_REG,   UUID(2), 1);
    ViewId v4(V_REG,   UUID(1), 2);
    ViewId v5(V_TRANS, UUID(1), 2);
    ViewId v6(V_REG,   UUID(2), 2);
    ViewId v7(V_TRANS, UUID(3), 2);

    fail_unless(v1 < v2);
    fail_unless(v2 < v3);
    fail_unless(v3 < v4);
    fail_unless(v5 < v6);
    fail_unless(v6 < v7);



    ViewId vid;
    fail_unless(vid.get_uuid() == UUID());
    fail_unless(vid.get_seq() == 0);
    
    UUID uuid(0, 0);
    
    vid = ViewId(V_REG, uuid, 7);
    fail_unless(vid.get_uuid() == uuid);
    fail_unless(vid.get_seq() == 7);
    
    check_serialization(vid, UUID::serial_size() + sizeof(uint32_t), ViewId());
    
    
    NodeList nl;

    for (size_t i = 0; i < 7; ++i)
    {
        nl.insert(make_pair(UUID(0, 0), Node()));
    }
    
    fail_unless(nl.size() == 7);
    check_serialization(nl, 4 + 7*(UUID::serial_size() 
                                   + Node::serial_size()), NodeList());
    
    
    View v(ViewId(V_TRANS, vid));

    for (size_t i = 0; i < 10; ++i)
    {
        UUID uuid(0, 0);
        string name("n" + gu::to_string(i));
        if (i < 3)
        {
            v.add_joined(uuid, name);
        }
        if (i < 7)
        {
            v.add_member(uuid, name);
        }
        else if (i < 9)
        {
            v.add_left(uuid, name);
        }
        else
        {
            v.add_partitioned(uuid, name);
        }

    }
        
    check_serialization(v, 
                        /* view id */
                        + ViewId::serial_size() 
                        /* 4 times node list length */
                        + 4*4 
                        /* 10 nodes which of 3 twice */
                        + (10 + 3)*(UUID::serial_size() 
                                    + Node::serial_size()),
                        View());
}
END_TEST


class T1
{
    int32_t foo;
public:
    
    T1(const int32_t foo_ = -1) : foo(foo_) {}
    
    size_t unserialize(const byte_t* buf, const size_t buflen, const size_t offset)
    {
        return gcomm::unserialize(buf, buflen, offset, &foo);
    }
    
    size_t serialize(byte_t* buf, const size_t buflen, const size_t offset) const
    {
        return gcomm::serialize(foo, buf, buflen, offset);
    }
    
    static size_t serial_size()
    {
        return 4;
    }

    string to_string() const
    {
        return gu::to_string(foo);
    }
    
    bool operator==(const T1& cmp) const
    {
        return foo == cmp.foo;
    }
};



class T3
{
public:
    T3() {}
};



START_TEST(test_map)
{
    typedef Map<IntType<int>, IntType<int>, std::map<IntType<int>, IntType<int> > > IntMap;

    IntMap m;
    check_serialization(m, 4, IntMap());

    m.insert(make_pair(make_int<int>(1), make_int<int>(2)));

}
END_TEST


Suite* types_suite()
{
    Suite* s = suite_create("types");
    TCase* tc;

    tc = tcase_create("test_sizes");
    tcase_add_test(tc, test_sizes);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_serialization");
    tcase_add_test(tc, test_serialization);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_uuid");
    tcase_add_test(tc, test_uuid);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_view");
    tcase_add_test(tc, test_view);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_map");
    tcase_add_test(tc, test_map);
    suite_add_tcase(s, tc);

    return s;
}
