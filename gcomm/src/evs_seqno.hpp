#ifndef EVS_SEQNO_HPP
#define EVS_SEQNO_HPP

#include <galerautils.hpp>

#include "gcomm/common.hpp"
#include "gcomm/types.hpp"

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

#endif // EVS_SEQNO_HPP
