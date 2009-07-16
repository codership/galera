#ifndef _GCOMM_PSEUDOFD_HPP_
#define _GCOMM_PSEUDOFD_HPP_

#include <gcomm/common.hpp>

BEGIN_GCOMM_NAMESPACE

struct PseudoFd
{
    static int alloc_fd();
    static void release_fd(int);
};

END_GCOMM_NAMESPACE

#endif // _GCOMM_PSEUDOFD_HPP_
