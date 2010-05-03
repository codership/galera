
#ifndef GALERA_SERIALIZATION_HPP
#define GALERA_SERIALIZATION_HPP

namespace galera
{
    // Helper templates to serialize integer types
    template<typename I> size_t serialize(const I& i,
                                          gu::byte_t* buf,
                                          size_t buf_len,
                                          size_t offset)
    {
        if (offset + sizeof(i) > buf_len) gu_throw_fatal;
        *reinterpret_cast<I*>(buf + offset) = i;
        return (offset + sizeof(i));
    }
    
    template<typename I> size_t unserialize(const gu::byte_t* buf,
                                            size_t buf_len,
                                            size_t offset,
                                            I& i)
    {
        if (offset + sizeof(i) > buf_len) gu_throw_fatal;
        i = *reinterpret_cast<const I*>(buf + offset);
        return (offset + sizeof(i));
    }
    
    
    template<typename I> size_t serial_size(const I& i)
    {
        return sizeof(i);
    }

    template<typename ST>
    size_t serialize(const gu::Buffer& b,
                     gu::byte_t* buf,
                     size_t buf_len,
                     size_t offset)
    {
        assert(b.size() <= std::numeric_limits<ST>::max());
        if (offset + b.size() > buf_len) gu_throw_fatal;
        offset = serialize(static_cast<ST>(b.size()), buf, buf_len, offset);
        copy(b.begin(), b.end(), buf + offset);
        offset += b.size();
        return offset;
    }
    
    template<typename ST>
    size_t unserialize(const gu::byte_t* buf,
                       size_t buf_len,
                       size_t offset,
                       gu::Buffer& b)
    {
        b.clear();
        ST len(0);
        if (offset + serial_size(len) > buf_len) gu_throw_fatal;
        
        offset = unserialize(buf, buf_len, offset, len);
        if (offset + len > buf_len) gu_throw_fatal;
        if (len > std::numeric_limits<ST>::max()) gu_throw_fatal;
        copy(buf + offset, buf + offset + len, b.begin());
        offset += len;
        return offset;
    }
    
    template<typename ST>
    size_t serial_size(const gu::Buffer& sb)
    {
        assert(sb.size() <= std::numeric_limits<ST>::max());
        return serial_size(ST()) + sb.size();
    }    


    template<class S, typename ST>
    size_t serialize(const S& s, gu::byte_t* buf, size_t buf_len, size_t offset)
    {
        if (s.size() > std::numeric_limits<ST>::max()) gu_throw_fatal;
        offset = serialize(static_cast<ST>(s.size()), buf, buf_len, offset);
        for (typename S::const_iterator i = s.begin(); i != s.end(); ++i)
        {
            offset = serialize(*i, buf, buf_len, offset);
        }
        return offset;
    }

    template<class S, typename ST>
    size_t unserialize(const gu::byte_t* buf, size_t buf_len, size_t offset,
                       S& s)
    {
        s.clear();
        ST len;
        offset = unserialize(buf, buf_len, offset, len);
        // s.reserve(len);
        for (ST i = 0; i < len; ++i)
        {
            typename S::value_type st;
            offset = unserialize(buf, buf_len, offset, st);
            s.push_back(st);
        }
        return offset;
    }

    template<class S, typename ST>
    size_t serial_size(const S& s)
    {
        size_t ret(serial_size(ST()));
        for (typename S::const_iterator i = s.begin(); i != s.end(); ++i)
        {
            ret += serial_size(*i);
        }
        return ret;
    }
}
#endif // GALERA_SERIALIZATION_HPP
