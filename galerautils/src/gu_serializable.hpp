// Copyright (C) 2013 Codership Oy <info@codership.com>
/**
 * @file Declaration of serializeble interface that all serializable classes
 *       should inherit.
 *
 * $Id$
 */

#ifndef GU_SERIALIZABLE_HPP
#define GU_SERIALIZABLE_HPP

#include "gu_types.hpp"
#include "gu_throw.hpp"
#include "gu_assert.hpp"

#include <vector>
#include <stdexcept> // for std::length_error

namespace gu
{

class Serializable
{
public:

    /*! returns the size of a buffer required to serialize the object */
    ssize_t serial_size () const
    {
        return my_serial_size();
    }

    /*!
     * serializes this object into buf and returns serialized size
     *
     * @param buf pointer to buffer
     * @param size size of buffer
     * @return serialized size
     *
     * may throw exceptions
     */
    ssize_t serialize_to (void* const buf, ssize_t const size) const
    {
        return my_serialize_to (buf, size);
    }

    /*!
     * serializes this object into byte vector v, reallocating it if needed
     * returns the size of serialized object
     */
    ssize_t serialize_to (std::vector<byte_t>& v) const
    {
        size_t const old_size (v.size());
        size_t const new_size (serial_size() + old_size);

        try
        {
            v.resize (new_size, 0);
        }
        catch (std::length_error& l)
        {
            gu_throw_error(EMSGSIZE) << "length_error: " << l.what();
        }
        catch (...)
        {
            gu_throw_error(ENOMEM) << "could not resize to " << new_size
                                   << " bytes";
        }

        try
        {
            return serialize_to (&v[old_size], new_size - old_size);
        }
        catch (...)
        {
            v.resize (old_size);
            throw;
        }
    }

protected:

    ~Serializable() {}

private:

    virtual ssize_t my_serial_size () const = 0;

    virtual ssize_t my_serialize_to (void* buf, ssize_t size) const = 0;
};

static inline std::vector<byte_t>&
operator << (std::vector<byte_t>& out, const Serializable& s)
{
    s.serialize_to (out);
    return out;
}

#if 0 // seems to be a pointless idea
class DeSerializable
{
public:

    /* serial size of an object stored at ptr, may be not implemented */
    template <class DS>
    static ssize_t serial_size (const byte_t* const buf, ssize_t const size)
    {
        assert (size > 0);
        return DS::my_serial_size (buf, size);
    }

    /* serial size of an object stored at ptr, may be not implemented */
    ssize_t deserialize_from (const byte_t* const buf, ssize_t const size)
    {
        assert (size > 0);
        return my_deserialize_from (buf, size);
    }

    ssize_t deserialize_from (const std::vector<byte_t>& in,size_t const offset)
    {
        return deserialize_from (&in[offset], in.size() - offset);
    }

protected:

    ~DeSerializable() {}

private:

    /* serial size of an object stored at ptr, may be not implemented */
    virtual ssize_t my_deserialize_from (const byte_t* buf, ssize_t size) = 0;
};
#endif // 0

} /* namespace gu */

#endif /* GU_SERIALIZABLE_HPP */
