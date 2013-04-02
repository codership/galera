/* Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _TEST_KEY_HPP_
#define _TEST_KEY_HPP_

#include "../src/key_data.hpp"
#include "../src/key_set.hpp" // for version_to_hash_size

#include <check.h> // for version_to_hash_size
#include <string.h>

#include <vector>

using namespace galera;

class TestKey
{
public:

    TestKey (int         ver,
             bool        exclusive,
             std::vector<const char*> parts,
             bool        nocopy = false)
        :
        parts_    (),
        ver_      (ver),
        exclusive_(exclusive),
        nocopy_   (nocopy)
    {
        parts_.reserve(parts.size());

        for (size_t i = 0; i < parts.size(); ++i)
        {
            wsrep_buf_t b = { parts[i], parts[i] ? strlen(parts[i]) + 1 : 0 };
            parts_.push_back(b);
        }
    }

    TestKey (int         ver,
             bool        exclusive,
             bool        nocopy,
             const char* part0,
             const char* part1 = 0,
             const char* part2 = 0,
             const char* part3 = 0,
             const char* part4 = 0,
             const char* part5 = 0,
             const char* part6 = 0,
             const char* part7 = 0,
             const char* part8 = 0,
             const char* part9 = 0
        )
        :
        parts_    (),
        ver_      (ver),
        exclusive_(exclusive),
        nocopy_   (nocopy)
    {
        parts_.reserve(10);

        (push_back(part0) &&
         push_back(part1) &&
         push_back(part2) &&
         push_back(part3) &&
         push_back(part4) &&
         push_back(part5) &&
         push_back(part6) &&
         push_back(part7) &&
         push_back(part8) &&
         push_back(part9));

    }

    KeyData
    operator() ()
    {
        return KeyData (ver_, parts_.data(), parts_.size(),
                        nocopy_, !exclusive_);
    }

private:

    std::vector<wsrep_buf_t> parts_;

    int  const ver_;
    bool const exclusive_;
    bool const nocopy_;

    bool
    push_back (const char* const p)
    {
        if (p)
        {
            wsrep_buf_t b = { p, strlen(p) + 1 };
            parts_.push_back(b);
            return true;
        }

        return false;
    }

};

enum
{
    SHARED = false,
    EXCLUSIVE = true
};

#endif /* _TEST_KEY_HPP_ */
