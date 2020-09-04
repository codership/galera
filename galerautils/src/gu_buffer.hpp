/*
 * Copyright (C) 2009-2017 Codership Oy <info@codership.com>
 */

/*!
 * Byte buffer class. This is thin wrapper to std::vector
 */

#ifndef GU_BUFFER_HPP
#define GU_BUFFER_HPP

#include "gu_types.hpp" // for gu::byte_t

#include "gu_shared_ptr.hpp"
#include <cassert>
#include <vector>

namespace gu
{
    /*
     * Utility class for data buffers with vector like interface.
     *
     * Additionally provides data() method to access underlying
     * data array. The call to data() is always valid, even if
     * the buffer is empty.
     */
    class Buffer
    {
    public:
        typedef std::vector<byte_t> buffer_type;
        typedef buffer_type::iterator iterator;
        typedef buffer_type::const_iterator const_iterator;
        Buffer() : buf_() { }
        Buffer(size_t size) : buf_(size) { }
        template <class InputIt>
        Buffer(InputIt first, InputIt last) : buf_(first, last) { }
        iterator begin() { return buf_.begin(); }
        iterator end() { return buf_.end(); }
        const_iterator begin() const { return buf_.begin(); }
        const_iterator end() const { return buf_.end(); }
        void insert(iterator pos, byte_t value)
        {
            buf_.insert(pos, value);
        }
        template <class InputIt>
        void insert(iterator pos, InputIt first, InputIt last)
        {
            buf_.insert(pos, first, last);
        }
        byte_t& operator[](size_t i)
        {
            assert(i < buf_.size());
            return buf_[i];
        }
        const byte_t& operator[](size_t i) const
        {
            assert(i < buf_.size());
            return buf_[i];
        }
        const byte_t* data() const
        {
            return (empty() ? 0 : &buf_[0]);
        }
        void resize(size_t size) { buf_.resize(size); }
        void reserve(size_t size) { buf_.reserve(size); }
        void clear() { buf_.clear(); }
        bool empty() const { return buf_.empty(); }
        size_t size() const { return buf_.size(); }
        bool operator==(const Buffer& other) const
        {
            return (buf_ == other.buf_);
        }
    private:
        std::vector<byte_t> buf_;
    };
    typedef gu::shared_ptr<Buffer>::type SharedBuffer;
}

#endif // GU_BUFFER_HPP
