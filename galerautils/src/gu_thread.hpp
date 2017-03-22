//
// Copyright (C) 2016-2017 Codership Oy <info@codership.com>
//

//
// Threading utilities
//

#ifndef GU_THREAD_HPP
#define GU_THREAD_HPP

#include "gu_threads.h"

#include <string>

namespace gu
{
    //
    // Wrapper class for thread scheduling parameters. For details,
    // about values see sched_setscheduler() and pthread_setschedparams()
    // documentation.
    //
    class ThreadSchedparam
    {
    public:
        //
        // Default constructor. Initializes to default system
        // scheduling parameters.
        //
        ThreadSchedparam()
            :
            policy_(SCHED_OTHER),
            prio_  (0)
        { }

        //
        // Construct ThreadSchedparam from given policy and priority
        // integer values.
        //
        ThreadSchedparam(int policy, int prio)
            :
            policy_(policy),
            prio_  (prio)
        { }

        //
        // Construct ThreadSchedparam from given string representation
        // which must have form of
        //
        //  <policy>:<priority>
        //
        // wehre policy is one of "other", "fifo", "rr" and priority
        // is an integer.
        //
        ThreadSchedparam(const std::string& param);

        // Return scheduling policy
        int policy() const { return policy_; }

        // Return scheduling priority
        int prio()   const { return prio_  ; }

        // Equal to operator overload
        bool operator==(const ThreadSchedparam& other) const
        {
            return (policy_ == other.policy_ && prio_ == other.prio_);
        }

        // Not equal to operator overload
        bool operator!=(const ThreadSchedparam& other) const
        {
            return !(*this == other);
        }

        // Default system ThreadSchedparam
        static ThreadSchedparam system_default;

        void print(std::ostream& os) const;

    private:

        int policy_; // Scheduling policy
        int prio_;   // Scheduling priority
    };

    //
    // Return current scheduling parameters for given thread
    //
    ThreadSchedparam thread_get_schedparam(gu_thread_t thread);

    //
    // Set scheduling parameters for given thread.
    //
    // Throws gu::Exception if setting parameters fails.
    //
    void thread_set_schedparam(gu_thread_t thread, const ThreadSchedparam&);

    //
    // Insertion operator for ThreadSchedparam
    //
    inline std::ostream& operator<<(std::ostream& os,
                                    const gu::ThreadSchedparam& sp)
    {
        sp.print(os); return os;
    }
}



#endif // GU_THREAD_HPP
