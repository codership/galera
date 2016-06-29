//
// Copyright (C) 2016 Codership Oy <info@codership.com>
//

#include "gu_thread.hpp"

#include "gu_utils.hpp"
#include "gu_string_utils.hpp"
#include "gu_throw.hpp"

#include <iostream>
#include <vector>

static inline void parse_thread_schedparam(const std::string& param,
                                           int& policy,
                                           int& prio)
{
    std::vector<std::string> sv(gu::strsplit(param, ':'));

    if (sv.size() != 2)
    {
        gu_throw_error(EINVAL) << "Invalid schedparam: " << param;
    }

    if      (sv[0] == "other") policy = SCHED_OTHER;
    else if (sv[0] == "fifo")  policy = SCHED_FIFO;
    else if (sv[0] == "rr")    policy = SCHED_RR;
    else gu_throw_error(EINVAL) << "Invalid scheduling policy: "
                                << sv[0];
    prio = gu::from_string<int>(sv[1]);
}

static inline std::string print_thread_schedparam(int policy, int prio)
{
    std::ostringstream oss;
    std::string policy_str;
    switch (policy)
    {
    case SCHED_OTHER: policy_str = "other"  ; break;
    case SCHED_FIFO:  policy_str = "fifo"   ; break;
    case SCHED_RR:    policy_str = "rr"     ; break;
    default:          policy_str = "unknown"; break;
    }

    oss << policy_str << ":" << prio;

    return oss.str();
}

gu::ThreadSchedparam gu::ThreadSchedparam::system_default(SCHED_OTHER, 0);

gu::ThreadSchedparam::ThreadSchedparam(const std::string& param)
    :
    policy_(),
    prio_  ()
{
    if (param == "")
    {
        *this = system_default;
    }
    else
    {
        parse_thread_schedparam(param, policy_, prio_);
    }
}


gu::ThreadSchedparam gu::thread_get_schedparam(pthread_t thd)
{
    int policy;
    struct sched_param sp;
    int err;
    if ((err = pthread_getschedparam(thd, &policy, &sp)) != 0)
    {
        gu_throw_error(err) << "Failed to read thread schedparams";
    }
    return ThreadSchedparam(policy, sp.sched_priority);
}

void gu::thread_set_schedparam(pthread_t thd, const gu::ThreadSchedparam& sp)
{
    struct sched_param spstr = { sp.prio() };
    int err;
    if ((err = pthread_setschedparam(thd, sp.policy(), &spstr)) != 0)
    {
        gu_throw_error(err) << "Failed to set thread schedparams " << sp;
    }
}


std::ostream& operator<<(std::ostream& os, const gu::ThreadSchedparam& sp)
{
    return (os << print_thread_schedparam(sp.policy(), sp.prio()));
}
