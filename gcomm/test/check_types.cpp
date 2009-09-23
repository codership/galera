
#include "check_gcomm.hpp"

#include "gcomm/view.hpp"
#include "gcomm/uri.hpp"
#include "gcomm/types.hpp"

#include "inst_map.hpp"
#include "gcomm/map.hpp"

#include <utility>
#include <iostream>
#include <stdexcept>

#include <galerautils.hpp>

using std::pair;
using std::make_pair;

#include "check_templ.hpp"

#include <check.h>

using namespace gcomm;


START_TEST(test_sizes)
{
    uint8_t u8(3);
    fail_unless(make_int(u8).size() == 1);

    uint16_t u16(3);
    fail_unless(make_int(u16).size() == 2);

    uint32_t u32(3);
    fail_unless(make_int(u32).size() == 4);

    uint64_t u64(3);
    fail_unless(make_int(u64).size() == 8);
    
}
END_TEST


START_TEST(test_relops)
{
    IntType<int> a(3), b(-7);

    fail_if(a == b);
    fail_unless(a != b);
    fail_unless(b < a);
    fail_unless(b <= a);
    fail_unless(a > b);
    fail_unless(a >= b);
    
    fail_unless(a >= a);
    fail_unless(a <= a);    
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
        LOG_DEBUG(uuidrnd.to_string());
    }

    UUID uuid1(0, 0);
    UUID uuid2(0, 0);

    fail_unless(uuid1 < uuid2);

}
END_TEST


START_TEST(test_view)
{

    ViewId vid;
    fail_unless(vid.get_uuid() == UUID());
    fail_unless(vid.get_seq() == 0);
    
    UUID uuid(0, 0);
    
    vid = ViewId(uuid, 7);
    fail_unless(vid.get_uuid() == uuid);
    fail_unless(vid.get_seq() == 7);
    
    check_serialization(vid, UUID::size() + 4, ViewId());
    
    
    NodeList nl;

    for (size_t i = 0; i < 7; ++i)
    {
        nl.insert(make_pair(UUID(0, 0), "n" + make_int(i).to_string()));
    }
    
    fail_unless(nl.length() == 7);
    check_serialization(nl, 4 + 7*(UUID::size() + NodeList::node_name_size), NodeList());


    View v(View::V_TRANS, vid);

    for (size_t i = 0; i < 10; ++i)
    {
        UUID uuid(0, 0);
        string name("n" + make_int(i).to_string());
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
                        /* Header (type etc) */
                        4 
                        /* view id */
                        + ViewId::size() 
                        /* 4 times node list length */
                        + 4*4 
                        /* 10 nodes which of 3 twice */
                        + (10 + 3)*(UUID::size() + NodeList::node_name_size),
                        View());
}
END_TEST


class T1
{
    int32_t foo;
public:
    
    T1(const int32_t foo_ = -1) : foo(foo_) {}
    
    size_t read(const byte_t* buf, const size_t buflen, const size_t offset)
    {
        return gcomm::read(buf, buflen, offset, &foo);
    }
    
    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const
    {
        return gcomm::write(foo, buf, buflen, offset);
    }
    
    static size_t size()
    {
        return 4;
    }

    string to_string() const
    {
        return make_int(foo).to_string();
    }
    
    bool operator==(const T1& cmp) const
    {
        return foo == cmp.foo;
    }
};


class T2
{
    static const size_t foolen = 16;
    string foo;
public:
    
    T2(const string foo_ = "") : foo(foo_, 0, foolen) {}
    
    size_t read(const byte_t* buf, const size_t buflen, const size_t offset)
    {
        byte_t b[foolen + 1];
        memset(b, 0, foolen + 1);
        size_t ret = read_bytes(buf, buflen, offset, b, foolen);
        if (ret != 0)
        {
            foo = string(reinterpret_cast<char*>(b));
        }
        return ret;
    }
    
    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const
    {
        byte_t b[foolen];
        memset(b, 0, foolen);
        strncpy(reinterpret_cast<char*>(b), foo.c_str(), foolen);
        return write_bytes(b, foolen, buf, buflen, offset);
    }
    
    static size_t size()
    {
        return foolen;
    }
    
    string to_string() const
    {
        return foo;
    }
    
    bool operator==(const T2& cmp) const
    {
        return foo == cmp.foo;
    }
};

class T3
{
public:
    T3() {}
};


START_TEST(test_inst_map)
{
    
    typedef InstMap<T1> T1Map;
    T1Map im1;
    
    im1.insert(make_pair(UUID(0, 0), T1(4)));
    fail_unless(im1.length() == 1);
    fail_unless(im1.size() == 24);

    check_serialization(im1, 24, T1Map());
    
    for (T1Map::const_iterator i = im1.begin(); i != im1.end(); ++i)
    {
        LOG_INFO(T1Map::get_uuid(i).to_string() +  " " 
                 + T1Map::get_instance(i).to_string());
    }


    typedef InstMap<T2> T2Map;
    T2Map im2;
    im2.insert(make_pair(UUID(0, 0), T2("strstrs strstrs b")));
    im2.insert(make_pair(UUID(0, 0), T2("strstrs strstrd ba")));
    fail_unless(im2.length() == 2);
    fail_unless(im2.size() == 68);

    check_serialization(im2, 68, T2Map());
     
    for (T2Map::const_iterator i = im2.begin(); i != im2.end(); ++i)
    {
        LOG_INFO(T2Map::get_uuid(i).to_string() +  " " 
                 + T2Map::get_instance(i).to_string());
    }
          
    typedef InstMap<T3> T3Map;
    T3Map im3;
    im3.insert(make_pair(UUID(0, 0), T3()));
    im3.insert(make_pair(UUID(0, 0), T3()));
    im3.insert(make_pair(UUID(0, 0), T3()));
    im3.insert(make_pair(UUID(0, 0), T3()));
    fail_unless(im3.length() == 4);

    for (T3Map::const_iterator i = im3.begin(); i != im3.end(); ++i)
    {
        LOG_INFO(T3Map::get_uuid(i).to_string());
    }


}
END_TEST

START_TEST(test_map)
{
    typedef Map<IntType<int>, IntType<int>, std::map<IntType<int>, IntType<int> > > IntMap;

    IntMap m;
    check_new_serialization(m, 4, IntMap());

    m.insert(make_pair(make_int<int>(1), make_int<int>(2)));

}
END_TEST

START_TEST(test_exception)
{

    try
    {
        throw std::logic_error(string("logic error message: ") + make_int(1).to_string());
    }
    catch (std::logic_error e)
    {
        log_info << e.what();
    }

}
END_TEST


Suite* types_suite()
{
    Suite* s = suite_create("types");
    TCase* tc;

    tc = tcase_create("test_sizes");
    tcase_add_test(tc, test_sizes);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_relops");
    tcase_add_test(tc, test_relops);
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

    tc = tcase_create("test_inst_map");
    tcase_add_test(tc, test_inst_map);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_map");
    tcase_add_test(tc, test_map);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_exception");
    tcase_add_test(tc, test_exception);
    suite_add_tcase(s, tc);

    return s;
}
