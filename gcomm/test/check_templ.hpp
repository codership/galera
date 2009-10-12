
#ifndef CHECK_TEMPL_HPP
#define CHECK_TEMPL_HPP

#include "gcomm/common.hpp"
#include "gcomm/types.hpp"
#include "gcomm/readbuf.hpp"
#include "gcomm/transport.hpp"
#include <check.h>

#include <deque>
#include <algorithm>

using std::deque;

BEGIN_GCOMM_NAMESPACE

template<class T>
void check_serialization(const T& c, const size_t expected_size, 
                         const T& default_c)
{

    fail_unless(c.size() == expected_size, 
                "size = %lu expected = %lu", 
                c.size(), expected_size);
    byte_t* buf = new byte_t[expected_size + 7];
    size_t ret;
    // Check that what is written gets also read
    try
    {
        (void)c.write(buf, expected_size, 1);
        fail("exception not thrown");
    }
    catch (RuntimeException& e)
    {
    }
    
    fail_unless(c.write(buf, expected_size, 0) == expected_size);
    
    T c2(default_c);

    try
    {
        (void)c2.read(buf, expected_size, 1);
        fail("exception not thrown");
    }
    catch (RuntimeException& e)
    {
    }
    
    ret = c2.read(buf, expected_size, 0);
    fail_unless(ret == expected_size, "expected %z ret %z", expected_size, ret);
    fail_unless(c == c2);
    
    // Check that read/write return offset properly
    
    fail_unless(c.write(buf, expected_size + 7, 5) == expected_size + 5);
    fail_unless(c2.read(buf, expected_size + 7, 5) == expected_size + 5);

    fail_unless(c == c2);
    
    delete[] buf;
}

template<class T>
void check_new_serialization(const T& c, const size_t expected_size, 
                             const T& default_c)
{
    
    fail_unless(c.serial_size() == expected_size, 
                "size = %lu expected = %lu", 
                c.serial_size(), expected_size);
    byte_t* buf = new byte_t[expected_size + 7];
    size_t ret;
    // Check that what is written gets also read
    try
    {
        (void)c.serialize(buf, expected_size, 1);
        fail("exception not thrown");
    }
    catch (RuntimeException& e)
    {
        // OK
    }
    fail_unless(c.serialize(buf, expected_size, 0) == expected_size);
    
    T c2(default_c);

    try
    {
        (void)c2.unserialize(buf, expected_size, 1);
        fail("exception not thrown");
    }
    catch (RuntimeException& e)
    {
        // OK
    }
    ret = c2.unserialize(buf, expected_size, 0);
    fail_unless(ret == expected_size, "expected %z ret %z", expected_size, ret);
    fail_unless(c == c2);
    
    // Check that read/write return offset properly
    
    fail_unless(c.serialize(buf, expected_size + 7, 5) == expected_size + 5);
    fail_unless(c2.unserialize(buf, expected_size + 7, 5) == expected_size + 5);
    
    fail_unless(c == c2);

    delete[] buf;
}



class DummyTransport : public Transport 
{
    deque<ReadBuf*> in;
    deque<ReadBuf*> out;
public:
    DummyTransport() : 
        Transport(URI("dummy:"), 0, 0),
        in(), out()
    {}
    
    ~DummyTransport() 
    {
        for (deque<ReadBuf*>::iterator i = in.begin(); i != in.end(); ++i)
            (*i)->release();
        for (deque<ReadBuf*>::iterator i = out.begin(); i != out.end(); ++i)
            (*i)->release();
    }
    
    size_t get_max_msg_size() const 
    {
        return (1U << 31);
    }
    
    void connect() 
    {
    }
    
    void close() 
    {
    }
    
    void listen() 
    {
        throw FatalException("Not applicable");
    }

    Transport *accept() 
    {
        throw FatalException("Not applicable");
    }

    void handle_up(const int cid, const ReadBuf* rb, const size_t roff, 
                   const ProtoUpMeta* um)
    {
        throw FatalException("not applicable");
    }
    
    int handle_down(WriteBuf *wb, const ProtoDownMeta *dm) 
    {
        out.push_back(wb->to_readbuf());
        return 0;
    }
    
    void pass_up(WriteBuf *wb, const ProtoUpMeta *um) 
    {
        in.push_back(wb->to_readbuf());
    }
    
    ReadBuf* get_out() 
    {
        if (out.empty())
            return 0;
        ReadBuf* rb = out.front();
        out.pop_front();
        return rb;
    }
};

END_GCOMM_NAMESPACE

#endif // CHECK_TEMPL_HPP
