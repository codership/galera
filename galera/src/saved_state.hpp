//
// Copyright (C) 2012-2018 Codership Oy <info@codership.com>
//

#ifndef GALERA_SAVED_STATE_HPP
#define GALERA_SAVED_STATE_HPP

#include "gu_atomic.hpp"
#include "gu_mutex.hpp"
#include "gu_lock.hpp"

#include "wsrep_api.h"

#include <string>
#include <cstdio>

namespace galera
{

class SavedState
{
public:

    SavedState  (const std::string& file);
    ~SavedState ();

    void get (wsrep_uuid_t& u, wsrep_seqno_t& s, bool& safe_to_bootstrap);
    void set (const wsrep_uuid_t& u, wsrep_seqno_t s, bool safe_to_bootstrap);

    void mark_unsafe();
    void mark_safe();
    void mark_corrupt();
    void mark_uncorrupt(const wsrep_uuid_t& u, wsrep_seqno_t s);

    bool corrupt() const { return corrupt_; }

    void stats(long& marks, long& locks, long& writes)
    {
        marks  = total_marks_();
        locks  = total_locks_;
        writes = total_writes_;
    }

private:

    FILE*               fs_;
    const std::string   filename_;
    wsrep_uuid_t        uuid_;
    wsrep_seqno_t       seqno_;
    bool                safe_to_bootstrap_;
    gu::Atomic<long>    unsafe_;
    bool                corrupt_;

    /* this mutex is needed because mark_safe() and mark_corrupt() will be
     * called outside local monitor, so race is possible */
    gu::Mutex           mtx_;
    wsrep_uuid_t        written_uuid_;
    ssize_t             current_len_;
    gu::Atomic<long>    total_marks_;
    long                total_locks_;
    long                total_writes_;

    void write_file (const wsrep_uuid_t& u, const wsrep_seqno_t s,
                     bool safe_to_bootstrap);

    SavedState (const SavedState&);
    SavedState& operator=(const SavedState&);

}; /* class SavedState */

} /* namespace galera */

#endif /* GALERA_SAVED_STATE_HPP */
