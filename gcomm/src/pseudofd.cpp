
#include "gcomm/pseudofd.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/monitor.hpp"

#include <set>
#include <limits>
#include <cassert>

BEGIN_GCOMM_NAMESPACE

static std::set<int> fds;

static const int min_fd = std::numeric_limits<int>::min();
static const int max_fd = -2;
static Monitor monitor;

int PseudoFd::alloc_fd()
{
    Critical crit(&monitor);
    int new_fd;
    std::set<int>::reverse_iterator i = fds.rbegin();
    if (i == fds.rend())
        new_fd = min_fd;
    else
        new_fd = *i + 1;

    std::pair<std::set<int>::iterator, bool> ret = fds.insert(new_fd);
    assert(ret.second == true);
    LOG_DEBUG(std::string("alloc fd: ") + Int(new_fd).to_string());
    return new_fd;
}

void PseudoFd::release_fd(const int fd)
{
    Critical crit(&monitor);
    LOG_DEBUG(std::string(std::string("release fd: ") + Int(fd).to_string()));
    assert(fds.find(fd) != fds.end());
    fds.erase(fd);
}

END_GCOMM_NAMESPACE
