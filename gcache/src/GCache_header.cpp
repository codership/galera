/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include <cstdio>
#include <cerrno>

#include "BufferHeader.hpp"
#include "Exception.hpp"
#include "Logger.hpp"
#include "GCache.hpp"

namespace gcache
{
   enum records {
        HEADER_LEN = 0,
        HEADER_VERSION,
        FILE_OPEN,
        FILE_SIZE,
        DATA_OFFSET,
        FIRST_OFFSET,
        NEXT_OFFSET,
        SEQNO_MIN,
        SEQNO_MAX
    };

    void
    GCache::header_read ()
    {
        std::ostringstream error;
        error << "Can't load gcache data file: ";

        if (version != (char)header[HEADER_VERSION]) {
            error << "unsupported version: " << header[HEADER_VERSION];
            throw Exception (error.str().c_str(), ECANCELED);
        }

        if (mmap.size != header[FILE_SIZE]) {
            error << "file size does not match, declared: " << header[FILE_SIZE]
                  << ", real: " << mmap.size;
            throw Exception (error.str().c_str(), ECANCELED);
        }

        if ((start - static_cast<uint8_t*>(mmap.ptr)) != 
            (int64_t)header[DATA_OFFSET]) {
            error << "data offset " << header[DATA_OFFSET]
                  << " does not match derived: "
                  << (start - static_cast<uint8_t*>(mmap.ptr));
            throw Exception (error.str().c_str(), ECANCELED);
        }

        if (true == header[FILE_OPEN]) {
            log_warn << "Gcache data file was not gracefully closed, "
                     << "discarding data.";
            reset_cache ();
            return;
        }

        if (size_cache <= header[FIRST_OFFSET]) {
            log_warn << "Bogus first buffer offset, discarding data.";
            reset_cache ();
            return;
        }

        if (size_cache <= header[NEXT_OFFSET]) {
            log_warn << "Bogus next buffer offset, discarding data.";
            reset_cache ();
            return;
        }

        seqno_min = header[SEQNO_MIN];
        seqno_max = header[SEQNO_MAX];

        if ((seqno_min == SEQNO_NONE && seqno_max != SEQNO_NONE) ||
            (seqno_min != SEQNO_NONE && seqno_max == SEQNO_NONE)) {
            log_warn << "Inconsistent seqno's: " << seqno_min << ", "
                     << seqno_max << ", discarding data.";
            reset_cache ();
            return;
        }

        if (seqno_min > seqno_max) {
            log_warn << "Minimum seqno > maximum seqno (" << seqno_min
                     << " > " << seqno_max << "), discarding data.";
            reset_cache ();
            return;
        }

        first = start + header[FIRST_OFFSET];
        next  = start + header[NEXT_OFFSET];

        /* Validate all buffers and populate seqno map */
        log_info << "Validating cached buffers...";
        uint8_t*      buf = first;
        BufferHeader* bh  = BH (buf);
        while (bh->size > 0) {
            if (bh->seqno != SEQNO_NONE) {
                seqno2ptr.insert (std::pair<int64_t, void*>(bh->seqno, bh + 1));
            }

            buf += bh->size;
            if (buf >= end) buf = start;
            bh = BH (buf);
        }

        if (buf != next) {
            log_warn << "Cache metadata corrupted: failed to validate "
                     << "allocated buffers. Discarding data.";
            reset_cache();
            return;
        }
        log_info << "Validating cached buffers done.";

        if (seqno_min != SEQNO_NONE) {
            log_info << "Checking for gaps in sequence numbers...";
            for (int64_t seqno = seqno_min; seqno <= seqno_max; seqno++) {
                if (seqno2ptr.find(seqno) == seqno2ptr.end()) {
                    log_warn << "Discontinuity in sequence numbers: " << seqno
                             << " is missing. Discarding data.";
                        reset_cache();
                    return;
                }
            }
            log_info << "Checking for gaps in sequence numbers...";
        }
    }

    void
    GCache::header_write ()
    {
        header[HEADER_LEN]     = header_len;
        header[HEADER_VERSION] = version;
        header[FILE_OPEN]      = open;
        header[FILE_SIZE]      = mmap.size;
        header[DATA_OFFSET]    = start - static_cast<uint8_t*>(mmap.ptr);
        header[FIRST_OFFSET]   = first - start;
        header[NEXT_OFFSET]    = next  - start;
        header[SEQNO_MIN]      = seqno_min;
        header[SEQNO_MAX]      = seqno_max;
    }

    void
    GCache::preamble_write ()
    {
        size_t written  = 0;
        size_t to_write = PREAMBLE_LEN;

        written += snprintf (preamble + written, to_write - written,
                             "* GCache data file *\n");

        written += snprintf (preamble + written, to_write - written,
                             "--------------------\n");

        written += snprintf (preamble + written, to_write - written,
                             "Version      : %llu\n", header[HEADER_VERSION]);

        written += snprintf (preamble + written, to_write - written,
                             "Size         : %llu bytes\n", header[FILE_SIZE]);

        written += snprintf (preamble + written, to_write - written,
                             "Closed       : %s\n",
                             header[FILE_OPEN] ? "no":"yes");

        written += snprintf (preamble + written, to_write - written,
                             "Data offset  : %llu\n", header[DATA_OFFSET]);

        written += snprintf (preamble + written, to_write - written,
                             "First buffer : %llu\n", header[FIRST_OFFSET]);

        written += snprintf (preamble + written, to_write - written,
                             "Next buffer  : %llu\n", header[NEXT_OFFSET]);

        written += snprintf (preamble + written, to_write - written,
                             "Min. seqno   : %llu\n", header[SEQNO_MIN]);

        written += snprintf (preamble + written, to_write - written,
                             "Max. seqno   : %llu\n", header[SEQNO_MAX]);

        written += snprintf (preamble + written, to_write - written,
                             "--------------------\n");
    }
}
