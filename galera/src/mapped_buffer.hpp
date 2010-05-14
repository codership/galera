//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include <string>

#include "gu_buffer.hpp"

namespace galera
{
    class MappedBuffer
    {
    public:
        typedef gu::byte_t& reference;
        typedef gu::byte_t const& const_reference;
        typedef gu::byte_t* iterator;
        typedef gu::byte_t const* const_iterator;
        
        MappedBuffer(const_iterator begin = 0,
                     const_iterator end = 0,
                     const std::string& working_dir = "/tmp", 
                     size_t threshold = 1 << 20);
        
        ~MappedBuffer();
        
        
        reference operator[](size_t i) { return buf_[i]; }
        const_reference operator[](size_t i) const { return buf_[i]; }
        
        void reserve(size_t sz);
        void resize(size_t sz);
        void clear();

        size_t size() const { return buf_size_; }
        
        iterator begin() { return buf_; }
        iterator end() { return (buf_ + buf_size_); }
        
        const_iterator begin() const { return buf_; }
        const_iterator end() const { return (buf_ + buf_size_); }
        
    private:
        
        MappedBuffer(const MappedBuffer&);
        void operator=(const MappedBuffer&);

        const std::string& working_dir_; // working dir for data files
        std::string  file_;            
        int          fd_;            // file descriptor
        size_t       threshold_;     // in-memory threshold
        gu::byte_t*  buf_;           // data buffer
        size_t       buf_size_;      // buffer size (inserted data size)
        size_t       real_buf_size_; // real buffer size (allocated size)
    };
}
