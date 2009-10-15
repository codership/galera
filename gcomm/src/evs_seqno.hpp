/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef EVS_SEQNO_HPP
#define EVS_SEQNO_HPP

#include <galerautils.hpp>

#include "gcomm/common.hpp"
#include "gcomm/types.hpp"

#include <ostream>

namespace gcomm
{
    namespace evs
    {
        class Seqno;
        std::ostream& operator<<(std::ostream&, const Seqno);

        class Range;
        std::ostream& operator<<(std::ostream&, const Range);
    }
}

/*!
 * Seqno class for overwrapping seqnos.
 */ 


class gcomm::evs::Seqno
{
public:
    Seqno(const uint16_t seq_ = seq_max) : seq(seq_) { }
    
    static const Seqno max() { return Seqno(seq_max); }
    
    Seqno& operator++()
    {
        gcomm_assert(seq != seq_max);
        seq = static_cast<uint16_t>((seq + 1) % seq_max);
        return *this;
    }
    
    Seqno operator+(const Seqno inc) const
    {
        gcomm_assert(inc.seq < seq_max/2);
        gcomm_assert(seq != seq_max);
        return static_cast<uint16_t>((seq + inc.seq) % seq_max);
    }
    
    Seqno operator-(const Seqno dec) const
    {
        gcomm_assert(dec.seq < seq_max/2);
        gcomm_assert(seq != seq_max);
        return static_cast<uint16_t>((uint32_t(seq) + seq_max - dec.seq) % seq_max);
    }

    bool operator==(const Seqno cmp) const
    {
        return cmp.seq == seq;
    }

    bool operator!=(const Seqno cmp) const
    {
        return cmp.seq != seq;
    }
    
    bool operator<(const Seqno cmp) const
    {
        gcomm_assert(seq != seq_max && cmp.seq != seq_max);
        if (cmp.seq < seq_max/2)
        {
            return seq < cmp.seq || seq > (cmp.seq + seq_max/2) % seq_max;
        }
        else
        {
            return seq < cmp.seq && seq > (cmp.seq + seq_max/2) % seq_max;
        }
    }
    
    bool operator>(const Seqno cmp) const
    {
        gcomm_assert(seq != seq_max && cmp.seq != seq_max);
        return not (*this < cmp || *this == cmp);
    }

    bool operator>=(const Seqno cmp) const
    {
        gcomm_assert(seq != seq_max && cmp.seq != seq_max);
        return (*this > cmp|| *this == cmp);
    }

    bool operator<=(const Seqno cmp) const
    {
        gcomm_assert(seq != seq_max && cmp.seq != seq_max);
        return (*this < cmp || *this == cmp);
    }
    


    uint16_t get() const { return seq; }
    
    size_t serialize(byte_t* buf, size_t buflen, size_t offset) const
    {
        gu_trace(offset = gcomm::serialize(seq, buf, buflen, offset));
        return offset;
    }
    
    size_t unserialize(const byte_t* buf, size_t buflen, size_t offset)
    {
        gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &seq));
        return offset;
    }
    
    static size_t serial_size()
    {
        return sizeof(uint16_t);
    }


    
private:
    uint16_t seq;
    static const uint16_t seq_max = 0x8000;
};

inline std::ostream& gcomm::evs::operator<<(std::ostream& os, const Seqno seq)
{
    return (os << seq.get());
}



/*!
 *
 */
class gcomm::evs::Range
{
public:
    Range(const Seqno lu_ = Seqno::max(), const Seqno hs_ = Seqno::max()) :
        lu(lu_),
        hs(hs_)
    {}
    Seqno get_lu() const { return lu; }
    Seqno get_hs() const { return hs; }
    
    void set_lu(const Seqno s) { lu = s; }
    void set_hs(const Seqno s) { hs = s; }
    
    size_t serialize(byte_t* buf, size_t buflen, size_t offset) const
    {
        gu_trace(offset = lu.serialize(buf, buflen, offset));
        gu_trace(offset = hs.serialize(buf, buflen, offset));
        return offset;
    }
    
    size_t unserialize(const byte_t* buf, size_t buflen, size_t offset)
    {
        gu_trace(offset = lu.unserialize(buf, buflen, offset));
        gu_trace(offset = hs.unserialize(buf, buflen, offset));
        return offset;
    }

    static size_t serial_size()
    {
        return 2*Seqno::serial_size();
    }

    bool operator==(const Range& cmp) const
    {
        return (lu == cmp.lu && hs == cmp.hs);
    }

private:
    Seqno lu; /*!< Lowest unseen seqno */
    Seqno hs; /*!< Highest seen seqno  */
};

inline std::ostream& gcomm::evs::operator<<(std::ostream& os, const gcomm::evs::Range r)
{
    return (os << "[" << r.get_lu() << "," << r.get_hs() << "]");
}

#endif // EVS_SEQNO_HPP
