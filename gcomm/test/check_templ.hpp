
#ifndef CHECK_TEMPL_HPP
#define CHECK_TEMPL_HPP

#include "gcomm/common.hpp"
#include "gcomm/types.hpp"
#include "gcomm/readbuf.hpp"
#include "gcomm/transport.hpp"
#include <check.h>

#include <deque>
#include <algorithm>

namespace gcomm
{

    inline void release_rb(ReadBuf* rb)
    {
        rb->release();
    }


    template <typename I>
    inline std::ostream& operator<<(std::ostream& os, const IntType<I>& i)
    {
        return (os << i.get());
    }

    
    template<class T>
    void check_serialization(const T& c, const size_t expected_size, 
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
        if ((c == c2) == false)
        {
            log_warn << "\n\t" << c << " !=\n\t" << c2;
        }
        fail_unless(c == c2);
    
        // Check that read/write return offset properly
    
        fail_unless(c.serialize(buf, expected_size + 7, 5) == expected_size + 5);
        fail_unless(c2.unserialize(buf, expected_size + 7, 5) == expected_size + 5);
    
        fail_unless(c == c2);

        delete[] buf;
    }



    class DummyTransport : public Transport 
    {
        UUID uuid;
        std::deque<ReadBuf*> out;
    public:
        DummyTransport(const UUID& uuid_ = UUID::nil()) :
            Transport(URI("dummy:"), 0, 0),            
            uuid(uuid_),
            out()
        {}
        
        ~DummyTransport() 
        {
            std::for_each(out.begin(), out.end(), release_rb);
            out.clear();
        }
    
        bool supports_uuid() const { return true; }
        
        const UUID& get_uuid() const { return uuid; }


        size_t get_max_msg_size() const { return (1U << 31); }
        
        void connect() { }
        
        void close() { }
    

        void listen() 
        {
            gcomm_throw_fatal << "not implemented";
        }
        
        Transport *accept() 
        {
            gcomm_throw_fatal << "not implemented";
            return 0;
        }
        
        void handle_up(int cid, const ReadBuf* rb, size_t roff, 
                       const ProtoUpMeta& um)
        {
            pass_up(rb, roff, um);
        }
        
        int handle_down(WriteBuf *wb, const ProtoDownMeta& dm) 
        {
            out.push_back(wb->to_readbuf());
            return 0;
        }
    
        
        
        ReadBuf* get_out() 
        {
            if (out.empty())
            {
                return 0;
            }
            ReadBuf* rb = out.front();
            out.pop_front();
            return rb;
        }
    };

    struct delete_object
    {
        template <typename T>
        void operator()(T* t)
        {
            delete t;
        }
    };

} // namespace gcomm

#endif // CHECK_TEMPL_HPP
