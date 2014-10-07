/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_CHECK_TEMPL_HPP
#define GCOMM_CHECK_TEMPL_HPP

#include "gcomm/types.hpp"
#include "gcomm/transport.hpp"
#include <check.h>

#include <deque>
#include <algorithm>

namespace gcomm
{


    template<class T>
    void check_serialization(const T& c, const size_t expected_size,
                             const T& default_c)
    {

        fail_unless(c.serial_size() == expected_size,
                    "size = %lu expected = %lu",
                    c.serial_size(), expected_size);
        gu::byte_t* buf = new gu::byte_t[expected_size + 7];
        size_t ret;
        // Check that what is written gets also read
        try
        {
            (void)c.serialize(buf, expected_size, 1);
            std::ostringstream os;
            os << c;
            fail("exception not thrown for %s", os.str().c_str());
        }
        catch (gu::Exception& e)
        {
            // OK
        }
        fail_unless(c.serialize(buf, expected_size, 0) == expected_size);

        T c2(default_c);

        // Commented out. This test happened to work because default
        // protocol version for messages was zero and if the second
        // byte of the buffer contained something else, exception was
        // thrown. Now that the version can be different from zero,
        // the outcome of this check depends on message structure.
        // try
        // {
        //     size_t res(c2.unserialize(buf, expected_size, 1));
        //     std::ostringstream os;
        //     os << c;
        //     fail("exception not thrown for %s, result %zu expected %zu",
        //          os.str().c_str(), res, expected_size);
        // }
        // catch (gu::Exception& e)
        // {
        //     // OK
        // }

        ret = c2.unserialize(buf, expected_size, 0);
        fail_unless(ret == expected_size,
                    "expected %zu ret %zu", expected_size, ret);
        if ((c == c2) == false)
        {
            log_warn << "\n\t" << c << " !=\n\t" << c2;
        }
        fail_unless(c == c2);

        // Check that read/write return offset properly

        fail_unless(c.serialize(buf, expected_size + 7, 5) == expected_size + 5);
        fail_unless(c2.unserialize(buf, expected_size + 7, 5) == expected_size + 5);

        fail_unless(c == c2);

        delete[] buf;
    }






} // namespace gcomm

#endif // CHECK_TEMPL_HPP
