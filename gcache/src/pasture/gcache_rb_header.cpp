/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#include "gcache_rb_header.hpp"

#include <galerautils.hpp>

#include <cstdio>
#include <cerrno>
#include <cstring>

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
    RingBuffer::header_read ()
    {
        std::ostringstream error;
        error << "Can't load gcache data file: ";

        if (version != static_cast<char>(header[HEADER_VERSION])) {
            error << "unsupported version: " << header[HEADER_VERSION];
            throw gu::Exception (error.str().c_str(), ECANCELED);
        }

        if (mmap.size != static_cast<size_t>(header[FILE_SIZE])) {
            error << "file size does not match, declared: " << header[FILE_SIZE]
                  << ", real: " << mmap.size;
            throw gu::Exception (error.str().c_str(), ECANCELED);
        }

        ssize_t data_offset = start - static_cast<uint8_t*>(mmap.ptr);
        if (data_offset != header[DATA_OFFSET]) {
            error << "data offset " << header[DATA_OFFSET]
                  << " does not match derived: " << data_offset;
            throw gu::Exception (error.str().c_str(), ECANCELED);
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
        BufferHeader* bh  = reinterpret_cast<BufferHeader*>(buf);

        while (bh->size > 0) {
            if (bh->seqno != SEQNO_NONE) {
                seqno2ptr.insert (std::pair<int64_t, void*>(bh->seqno, bh + 1));
            }

            if (!BH_is_released (bh)) {
                log_warn << "Unreleased buffer found. Releasing.";
                BH_release (bh);
            }

            buf += bh->size;

            if (buf > (end - sizeof(BufferHeader))) break;

            bh = reinterpret_cast<BufferHeader*>(buf);

            if (0 == bh->size && buf != next) {
                // buffer list continues from the beginning
                buf = start;
                bh = reinterpret_cast<BufferHeader*>(buf);
            }
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
    RingBuffer::header_write ()
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
    RingBuffer::preamble_write ()
    {
        std::ostringstream pstream;

        pstream
            << "* GCache data file *" << std::endl
            << "--------------------" << std::endl
            << "Version         : " << header[HEADER_VERSION]       << std::endl
            << "Size            : " << header[FILE_SIZE] << "bytes" << std::endl
            << "Closed          : " << (header[FILE_OPEN]?"no":"yes") << std::endl
            << "Data offset     : " << header[DATA_OFFSET]  << std::endl
            << "First buffer    : " << header[FIRST_OFFSET] << std::endl
            << "Next buffer     : " << header[NEXT_OFFSET]  << std::endl
            << "Min. seqno      : " << header[SEQNO_MIN]    << std::endl
            << "Max. seqno      : " << header[SEQNO_MAX]    << std::endl
            << "Ordered buffers : " << (header[SEQNO_MAX] - header[SEQNO_MIN]) << std::endl
            << "--------------------" << std::endl;

        strncpy (preamble, pstream.str().c_str(), PREAMBLE_LEN - 1);
        preamble[PREAMBLE_LEN - 1] = '\0';
    }
}
