/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */
#ifndef _GCOMM_TIME_HPP_
#define _GCOMM_TIME_HPP_

#include "gu_datetime.hpp"

namespace gcomm
{
    typedef gu::datetime::Date Time;
    typedef gu::datetime::Period Period;


    inline void Sleep(const Period& p)
    {
        usleep(static_cast<useconds_t>(p.get_utc()/gu::datetime::USec));
    }

} // namespace gcomm

#endif // _GCOMM_TIME_HPP_
