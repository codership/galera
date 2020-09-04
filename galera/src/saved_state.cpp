//
// Copyright (C) 2012-2018 Codership Oy <info@codership.com>
//

#include "saved_state.hpp"
#include <gu_dbug.h>
#include <gu_uuid.hpp>
#include "gu_inttypes.hpp"

#include <fstream>

#include <sys/file.h>
#include <fcntl.h>

namespace galera
{

#define VERSION "2.1"
#define MAX_SIZE 256

SavedState::SavedState  (const std::string& file) :
    fs_           (0),
    filename_     (file),
    uuid_         (WSREP_UUID_UNDEFINED),
    seqno_        (WSREP_SEQNO_UNDEFINED),
    safe_to_bootstrap_(true),
    unsafe_       (0),
    corrupt_      (false),
    mtx_          (),
    written_uuid_ (uuid_),
    current_len_  (0),
    total_marks_  (0),
    total_locks_  (0),
    total_writes_ (0)
{

    GU_DBUG_EXECUTE("galera_init_invalidate_state",
                    unlink(file.c_str()););

    std::ifstream ifs(file.c_str());

    if (ifs.fail())
    {
        log_warn << "Could not open state file for reading: '" << file << '\'';
    }

    fs_ = fopen(file.c_str(), "a");

    if (!fs_)
    {
        gu_throw_error(errno)
            << "Could not open state file for writing: '" << file
            << "'. Check permissions and/or disk space.";
    }

    // We take exclusive lock on state file in order to avoid possibility
    // of two Galera replicators sharing the same state file.
    struct flock flck;
    flck.l_start  = 0;
    flck.l_len    = 0;
    flck.l_type   = F_WRLCK;
    flck.l_whence = SEEK_SET;

    if (::fcntl(fileno(fs_), F_SETLK, &flck))
    {
        log_warn << "Could not get exclusive lock on state file: " << file
                 << ": " << ::strerror(errno);
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
        else if (param == "safe_to_bootstrap:")
        {
            istr >> safe_to_bootstrap_;
            log_debug << "read safe_to_bootstrap: " << safe_to_bootstrap_;
        }
    }

    log_info << "Found saved state: " << uuid_ << ':' << seqno_
             << ", safe_to_bootstrap: " << safe_to_bootstrap_;

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
        set (uuid_, seqno_, safe_to_bootstrap_);
    }
}

SavedState::~SavedState ()
{
    if (fs_)
    {
        // Closing file descriptor should release the lock, but still...
        struct flock flck;
        flck.l_start  = 0;
        flck.l_len    = 0;
        flck.l_type   = F_UNLCK;
        flck.l_whence = SEEK_SET;

        if (::fcntl(fileno(fs_), F_SETLK, &flck))
        {
            log_warn << "Could not unlock state file: " << ::strerror(errno);
        }

        fclose(fs_);
    }
}

void
SavedState::get (wsrep_uuid_t& u, wsrep_seqno_t& s, bool& safe_to_bootstrap)
{
    gu::Lock lock(mtx_);

    u = uuid_;
    s = seqno_;
    safe_to_bootstrap = safe_to_bootstrap_;
}

void
SavedState::set (const wsrep_uuid_t& u, wsrep_seqno_t s, bool safe_to_bootstrap)
{
    gu::Lock lock(mtx_); ++total_locks_;

    if (corrupt_) return;

    uuid_ = u;
    seqno_ = s;
    safe_to_bootstrap_ = safe_to_bootstrap;

    if (0 == unsafe_())
        write_file (u, s, safe_to_bootstrap);
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
            write_file (WSREP_UUID_UNDEFINED, WSREP_SEQNO_UNDEFINED,
                        safe_to_bootstrap_);
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

        if (0 == unsafe_() && (written_uuid_ != uuid_ || seqno_ >= 0) &&
            !corrupt_)
        {
            /* this will write down proper seqno if set() was called too early
             * (in unsafe state) */
            write_file (uuid_, seqno_, safe_to_bootstrap_);
        }
    }
}

void
SavedState::mark_corrupt()
{
    gu::Lock lock(mtx_); ++total_locks_;

    if (corrupt_) return;

    uuid_  = WSREP_UUID_UNDEFINED;
    seqno_ = WSREP_SEQNO_UNDEFINED;
    corrupt_ = true;

    write_file (WSREP_UUID_UNDEFINED, WSREP_SEQNO_UNDEFINED,
                safe_to_bootstrap_);
}

void
SavedState::mark_uncorrupt(const wsrep_uuid_t& u, wsrep_seqno_t s)
{
    gu::Lock lock(mtx_); ++total_locks_;

    if (!corrupt_) return;

    uuid_    = u;
    seqno_   = s;
    unsafe_  = 0;
    corrupt_ = false;

    write_file (u, s, safe_to_bootstrap_);
}

void
SavedState::write_file(const wsrep_uuid_t& u, const wsrep_seqno_t s,
                       bool safe_to_bootstrap)
{
    assert (current_len_ <= MAX_SIZE);

    if (fs_)
    {
        if (s >= 0) { log_debug << "Saving state: " << u << ':' << s; }

        char buf[MAX_SIZE];
        int state_len = snprintf (buf, MAX_SIZE - 1,
                                  "# GALERA saved state"
                                  "\nversion: " VERSION
                                  "\nuuid:    " GU_UUID_FORMAT
                                  "\nseqno:   %" PRId64
                                  "\nsafe_to_bootstrap: %d\n",
                                  GU_UUID_ARGS(&u), s, safe_to_bootstrap);

        int write_size;
        for (write_size = state_len; write_size < current_len_; ++write_size)
            buf[write_size] = ' '; // overwrite whatever is there currently

        rewind(fs_);

        if (fwrite(buf, write_size, 1, fs_) == 0) {
            log_warn << "write file(" << filename_ << ") failed("
                     << strerror(errno) << ")";
            return;
        }

        if (fflush(fs_) != 0) {
            log_warn << "fflush file(" << filename_ << ") failed("
                     << strerror(errno) << ")";
            return;
        }

        if (fsync(fileno(fs_)) < 0) {
            log_warn << "fsync file(" << filename_ << ") failed("
                     << strerror(errno) << ")";
            return;
        }

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

