
#include "gcomm/pseudofd.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/monitor.hpp"

#include <set>
#include <limits>

using namespace std;
using namespace gcomm;

static set<int> fds;

/* we can have another set<int> for released fds and recycle them */

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
    
    log_debug << "alloc fd: " << new_fd;
    
    return new_fd;
}

void PseudoFd::release_fd(const int fd)
{
    Critical crit(&monitor);
    
    log_debug << "release fd: " << fd;

    assert(fds.find(fd) != fds.end());
    
    fds.erase(fd);
}


