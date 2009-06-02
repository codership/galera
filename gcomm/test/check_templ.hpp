
#ifndef CHECK_TEMPL_HPP
#define CHECK_TEMPL_HPP

#include "gcomm/common.hpp"
#include "gcomm/types.hpp"
#include <check.h>

BEGIN_GCOMM_NAMESPACE

template<class T>
void check_serialization(const T& c, const size_t expected_size, 
                         const T& default_c)
{

    fail_unless(c.size() == expected_size, "size = %z expected = %z", c.size(), expected_size);
    byte_t* buf = new byte_t[expected_size + 7];

    // Check that what is written gets also read
    fail_unless(c.write(buf, expected_size, 1) == 0);
    fail_unless(c.write(buf, expected_size, 0) == expected_size);
    
    T c2(default_c);

    fail_unless(c2.read(buf, expected_size, 1) == 0);
    fail_unless(c2.read(buf, expected_size, 0) == expected_size);
    
    fail_unless(c == c2);
    
    // Check that read/write return offset properly
    
    fail_unless(c.write(buf, expected_size + 7, 5) == expected_size + 5);
    fail_unless(c2.read(buf, expected_size + 7, 5) == expected_size + 5);

    fail_unless(c == c2);

    delete[] buf;
}

END_GCOMM_NAMESPACE

#endif // CHECK_TEMPL_HPP
