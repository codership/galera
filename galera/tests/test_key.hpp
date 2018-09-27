/* Copyright (C) 2013-2018 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _TEST_KEY_HPP_
#define _TEST_KEY_HPP_

#include "../src/key_data.hpp"
#include "../src/key_set.hpp" // for version_to_hash_size

#include <string.h>

#include <vector>

using namespace galera;

class TestKey
{
public:

    TestKey (int a, int  ver,
             wsrep_key_type_t type,
             std::vector<const char*> parts,
             bool        copy = true)
        :
        parts_    (),
        ver_      (ver),
        type_     (type),
        copy_     (copy)
    {
        parts_.reserve(parts.size());

        for (size_t i = 0; i < parts.size(); ++i)
        {
            size_t p_len(parts[i] ? strlen(parts[i]) + 1 : 0);
            wsrep_buf_t b = { parts[i], p_len };
            parts_.push_back(b);
        }
    }

    TestKey (int         ver,
             wsrep_key_type_t type,
             bool        copy,
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
        type_     (type),
        copy_     (copy)
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
        return KeyData (ver_, parts_.data(), parts_.size(), type_, copy_);
    }

private:

    std::vector<wsrep_buf_t> parts_;

    int  const ver_;
    wsrep_key_type_t const type_;
    bool const copy_;

    bool
    push_back (const char* const p)
    {
        size_t p_len(-1);
        if (p && (p_len = strlen(p) + 1) > 0)
        {
            wsrep_buf_t b = { p, p_len };
            parts_.push_back(b);
            return true;
        }

        return false;
    }

};

#endif /* _TEST_KEY_HPP_ */
