//
// Copyright (C) 2016 Codership Oy <info@codership.com>
//

#include "gu_thread.hpp"

#include "gu_utils.hpp"
#include "gu_string_utils.hpp"
#include "gu_throw.hpp"
#include "gu_logger.hpp"

#include <iostream>
#include <vector>

static std::string const SCHED_OTHER_STR  ("other");
static std::string const SCHED_FIFO_STR   ("fifo");
static std::string const SCHED_RR_STR     ("rr");
static std::string const SCHED_UNKNOWN_STR("unknown");

static inline void parse_thread_schedparam(const std::string& param,
                                           int& policy,
                                           int& prio)
{
    std::vector<std::string> sv(gu::strsplit(param, ':'));

    if (sv.size() != 2)
    {
        gu_throw_error(EINVAL) << "Invalid schedparam: " << param;
    }

    if      (sv[0] == SCHED_OTHER_STR) policy = SCHED_OTHER;
    else if (sv[0] == SCHED_FIFO_STR)  policy = SCHED_FIFO;
    else if (sv[0] == SCHED_RR_STR)    policy = SCHED_RR;
    else gu_throw_error(EINVAL) << "Invalid scheduling policy: " << sv[0];

    prio = gu::from_string<int>(sv[1]);
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

void gu::ThreadSchedparam::print(std::ostream& os) const
{
    std::string policy_str;
    switch (policy())
    {
    case SCHED_OTHER: policy_str = SCHED_OTHER_STR  ; break;
    case SCHED_FIFO:  policy_str = SCHED_FIFO_STR   ; break;
    case SCHED_RR:    policy_str = SCHED_RR_STR     ; break;
    default:          policy_str = SCHED_UNKNOWN_STR; break;
    }

    os << policy_str << ":" << prio();
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

static bool schedparam_not_supported(false);

void gu::thread_set_schedparam(pthread_t thd, const gu::ThreadSchedparam& sp)
{
    if (schedparam_not_supported) return;
#if defined(__sun__)
    struct sched_param spstr = { sp.prio(), { 0, } /* sched_pad array */};
#else
    struct sched_param spstr = { sp.prio() };
#endif
    int err;
    if ((err = pthread_setschedparam(thd, sp.policy(), &spstr)) != 0)
    {
        if (err == ENOSYS)
        {
            log_warn << "Function pthread_setschedparam() is not implemented "
                     << "in this system. Future attempts to change scheduling "
                     << "priority will be no-op";

            schedparam_not_supported = true;
        }
        else
        {
            gu_throw_error(err) << "Failed to set thread schedparams " << sp;
        }
    }
}
