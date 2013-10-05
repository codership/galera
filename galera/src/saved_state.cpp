//
// Copyright (C) 2012 Codership Oy <info@codership.com>
//

#include "saved_state.hpp"

#include "uuid.hpp"

#include <fstream>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

namespace galera
{

#define VERSION "2.1"
#define MAX_SIZE 256

SavedState::SavedState  (const std::string& file) :
    fs_           (0),
    uuid_         (WSREP_UUID_UNDEFINED),
    seqno_        (WSREP_SEQNO_UNDEFINED),
    unsafe_       (0),
    corrupt_      (false),
    mtx_          (),
    written_uuid_ (uuid_),
    current_len_  (0),
    total_marks_  (0),
    total_locks_  (0),
    total_writes_ (0)
{
    std::ifstream ifs(file.c_str());
    std::ofstream ofs;

    if (ifs.fail())
    {
        log_warn << "Could not open saved state file for reading: " << file;
    }

    fs_ = fopen(file.c_str(), "a");

    if (!fs_)
    {
        log_warn << "Could not open saved state file for writing: " << file;
        /* We are not reading anything from file we can't write to, since it
           may be terribly outdated. */
        return;
    }

    std::string version("0.8");
    std::string line;

    while (getline(ifs, line), ifs.good())
    {
        std::istringstream istr(line);
        std::string        param;

        istr >> param;

        if (param[0] == '#')
        {
            log_debug << "read comment: " << line;
        }
        else if (param == "version:")
        {
            istr >> version; // nothing to do with this yet
            log_debug << "read version: " << version;
        }
        else if (param == "uuid:")
        {
            try
            {
                istr >> uuid_;
                log_debug << "read saved state uuid: " << uuid_;
            }
            catch (gu::Exception& e)
            {
                log_error << e.what();
                uuid_ = WSREP_UUID_UNDEFINED;
            }
        }
        else if (param == "seqno:")
        {
            istr >> seqno_;
            log_debug << "read saved state seqno: " << seqno_;
        }
        else if (param == "cert_index:")
        {
            // @todo
            log_debug << "cert index restore not implemented yet";
        }
    }

    log_info << "Found saved state: " << uuid_ << ':' << seqno_;

#if 0 // we'll probably have it legal
    if (seqno_ < 0 && uuid_ != WSREP_UUID_UNDEFINED)
    {
        log_warn << "Negative seqno with valid UUID: "
                 << uuid_ << ':' << seqno_ << ". Discarding UUID.";
        uuid_ = WSREP_UUID_UNDEFINED;
    }
#endif

    written_uuid_ = uuid_;

    current_len_ = ftell (fs_);
    log_debug << "Initialized current_len_ to " << current_len_;
    if (current_len_ <= MAX_SIZE)
    {
        fs_ = freopen (file.c_str(), "r+", fs_);
    }
    else // normalize file contents
    {
        fs_ = freopen (file.c_str(), "w+", fs_); // truncate
        current_len_ = 0;
        set (uuid_, seqno_);
    }
}

SavedState::~SavedState ()
{
    if (fs_) fclose(fs_);
}

void
SavedState::get (wsrep_uuid_t& u, wsrep_seqno_t& s)
{
    gu::Lock lock(mtx_);

    u = uuid_;
    s = seqno_;
}

void
SavedState::set (const wsrep_uuid_t& u, wsrep_seqno_t s)
{
    gu::Lock lock(mtx_); ++total_locks_;

    if (corrupt_) return;

    uuid_ = u;
    seqno_ = s;

    if (0 == unsafe_())
        write_and_flush (u, s);
    else
        log_debug << "Not writing state: unsafe counter is " << unsafe_();
}

/* the goal of unsafe_, written_uuid_, current_len_ below is
 * 1. avoid unnecessary mutex locks
 * 2. if locked - avoid unnecessary file writes
 * 3. if writing - avoid metadata operations, write over existing space */

void
SavedState::mark_unsafe()
{
    ++total_marks_;

    if (1 == unsafe_.add_and_fetch (1))
    {
        gu::Lock lock(mtx_); ++total_locks_;

        assert (unsafe_() > 0);

        if (written_uuid_ != WSREP_UUID_UNDEFINED)
        {
            write_and_flush (WSREP_UUID_UNDEFINED, WSREP_SEQNO_UNDEFINED);
        }
    }
}

void
SavedState::mark_safe()
{
    ++total_marks_;

    long count = unsafe_.sub_and_fetch (1);
    assert (count >= 0);

    if (0 == count)
    {
        gu::Lock lock(mtx_); ++total_locks_;

        if (0 == unsafe_() && (written_uuid_ != uuid_ || seqno_ >= 0))
        {
            assert(false == corrupt_);
            /* this will write down proper seqno if set() was called too early
             * (in unsafe state) */
            write_and_flush (uuid_, seqno_);
        }
    }
}

void
SavedState::mark_corrupt()
{
    /* Half LONG_MAX keeps us equally far from overflow and underflow by
       mark_unsafe()/mark_safe() calls */
    unsafe_ = (std::numeric_limits<long>::max() >> 1);

    gu::Lock lock(mtx_); ++total_locks_;

    if (corrupt_) return;

    uuid_  = WSREP_UUID_UNDEFINED;
    seqno_ = WSREP_SEQNO_UNDEFINED;
    corrupt_ = true;

    write_and_flush (WSREP_UUID_UNDEFINED, WSREP_SEQNO_UNDEFINED);
}

void
SavedState::write_and_flush(const wsrep_uuid_t& u, const wsrep_seqno_t s)
{
    assert (current_len_ <= MAX_SIZE);

    if (fs_)
    {
        if (s >= 0) { log_debug << "Saving state: " << u << ':' << s; }

        char buf[MAX_SIZE];
        const gu_uuid_t* const uu(reinterpret_cast<const gu_uuid_t*>(&u));
        int state_len = snprintf (buf, MAX_SIZE - 1,
                                  "# GALERA saved state"
                                  "\nversion: " VERSION
                                  "\nuuid:    " GU_UUID_FORMAT
                                  "\nseqno:   %" PRId64 "\ncert_index:\n",
                                  GU_UUID_ARGS(uu), s);

        int write_size;
        for (write_size = state_len; write_size < current_len_; ++write_size)
            buf[write_size] = ' '; // overwrite whatever is there currently

        rewind(fs_);
        fwrite(buf, write_size, 1, fs_);
        fflush(fs_);

        current_len_ = state_len;
        written_uuid_ = u;
        ++total_writes_;
    }
    else
    {
        log_debug << "Can't save state: output stream is not open.";
    }
}

} /* namespace galera */

