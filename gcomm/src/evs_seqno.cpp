
#include "evs_seqno.hpp"

#include "gcomm/types.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/exception.hpp"

BEGIN_GCOMM_NAMESPACE

static const uint32_t MAX_SEQNO_MAX = 0x80000000U;
uint32_t _SEQNO_MAX = MAX_SEQNO_MAX;

void set_seqno_max(const uint32_t val)
{
    if (val > MAX_SEQNO_MAX)
    {
        LOG_FATAL("seqno max too large: " + UInt32(val).to_string());
        throw FatalException("seqno max too large");
    }
    if (val != 0)
    {
        _SEQNO_MAX = val;
    }
    else
    {
        _SEQNO_MAX = MAX_SEQNO_MAX;
    }
}

END_GCOMM_NAMESPACE
