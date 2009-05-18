#ifndef PSEUDOFD_HPP
#define PSEUDOFD_HPP

#include <gcomm/common.hpp>

BEGIN_GCOMM_NAMESPACE

struct PseudoFd
{
    static int alloc_fd();
    static void release_fd(int);
};

END_GCOMM_NAMESPACE

#endif // PSEUDOFD_HPP
