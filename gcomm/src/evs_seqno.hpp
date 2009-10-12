#ifndef EVS_SEQNO_HPP
#define EVS_SEQNO_HPP

#include <galerautils.hpp>

#include "gcomm/common.hpp"
#include "gcomm/types.hpp"

#include <ostream>

BEGIN_GCOMM_NAMESPACE

extern uint32_t _SEQNO_MAX;

void set_seqno_max(uint32_t);

#define SEQNO_MAX _SEQNO_MAX 

static inline uint32_t seqno_next(const uint32_t seq)
{
    assert(seq != SEQNO_MAX);
    return (seq + 1) % SEQNO_MAX;
}

static inline uint32_t seqno_add(const uint32_t seq, const uint32_t add)
{
    assert(seq != SEQNO_MAX && add != SEQNO_MAX);
    return (seq + add) % SEQNO_MAX;
}

static inline uint32_t seqno_dec(const uint32_t seq, const uint32_t dec)
{
    assert(seq != SEQNO_MAX && dec != SEQNO_MAX);
    return (seq - dec) % SEQNO_MAX;
}

static inline bool seqno_eq(const uint32_t a, const uint32_t b)
{
    return a == b;
}

static inline bool seqno_lt(const uint32_t a, const uint32_t b)
{
    assert(a != SEQNO_MAX && b != SEQNO_MAX);
    
    if (b < SEQNO_MAX/2)
	return a < b || a > seqno_add(b, SEQNO_MAX/2);
    else
	return a < b && a > seqno_add(b, SEQNO_MAX/2);
}

static inline bool seqno_gt(const uint32_t a, const uint32_t b)
{
    assert(a != SEQNO_MAX && b != SEQNO_MAX);
    return !(seqno_lt(a, b) || seqno_eq(a, b));
}

END_GCOMM_NAMESPACE


namespace gcomm
{
    namespace evs
    {
        class Seqno;
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
        assert(seq != seq_max);
        seq = static_cast<uint16_t>((seq + 1) % seq_max);
        return *this;
    }
    
    Seqno operator+(const Seqno inc) const
    {
        assert(inc.seq < seq_max/2);
        assert(seq != seq_max);
        return static_cast<uint16_t>((seq + inc.seq) % seq_max);
    }
    
    Seqno operator-(const Seqno dec) const
    {
        assert(dec.seq < seq_max/2);
        assert(seq != seq_max);
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
        assert(seq != seq_max && cmp.seq != seq_max);
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
        assert(seq != seq_max && cmp.seq != seq_max);
        return not (*this < cmp || *this == cmp);
    }

    bool operator>=(const Seqno cmp) const
    {
        assert(seq != seq_max && cmp.seq != seq_max);
        return (*this > cmp|| *this == cmp);
    }

    bool operator<=(const Seqno cmp) const
    {
        assert(seq != seq_max && cmp.seq != seq_max);
        return (*this < cmp || *this == cmp);
    }
    


    uint16_t get() const { return seq; }
    
private:
    uint16_t seq;
    static const uint16_t seq_max = 0xffff;
};

inline std::ostream& operator<<(std::ostream& os, const gcomm::evs::Seqno seq)
{
    return (os << seq.get());
}


#endif // EVS_SEQNO_HPP
