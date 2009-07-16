/**
 * Default poll implementation 
 */

#include "galeracomm/poll.hpp"
#include "galeracomm/fifo.hpp"
#include "galeracomm/logger.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <poll.h>
#include <map>
#include <iostream>

class PollDef : public Poll {
    std::map<const int, PollContext *> ctx_map;
    size_t n_pfds;
    struct pollfd *pfds;
public:
    PollDef();
    ~PollDef();
    void insert(const int, PollContext *);
    void erase(const int);
    void set(const int, const PollEnum);
    void unset(const int, const PollEnum);
    int poll(const int);
};



static inline int map_event_to_mask(const PollEnum e)
{
    int ret = (e & PollEvent::POLL_IN) ? POLLIN : 0;
    ret |= ((e & PollEvent::POLL_OUT) ? POLLOUT : 0);
    return ret;
}

static inline PollEnum map_mask_to_event(const int m)
{
    PollEnum ret = PollEvent::POLL_NONE;
    ret |= (m & POLLIN) ? PollEvent::POLL_IN : PollEvent::POLL_NONE;
    ret |= (m & POLLOUT) ? PollEvent::POLL_OUT : PollEvent::POLL_NONE;
    ret |= (m & POLLERR) ? PollEvent::POLL_ERR : PollEvent::POLL_NONE;
    ret |= (m & POLLHUP) ? PollEvent::POLL_HUP : PollEvent::POLL_NONE;
    ret |= (m & POLLNVAL) ? PollEvent::POLL_INVAL : PollEvent::POLL_NONE;
    return ret;
}

static struct pollfd *pfd_find(struct pollfd *pfds, const size_t n_pfds, 
			       const int fd)
{
    for (size_t i = 0; i < n_pfds; i++) {
	if (pfds[i].fd == fd)
	    return &pfds[i];
    }
    return 0;
}


void PollDef::insert(const int fd, PollContext *pctx)
{

    if (ctx_map.insert(std::pair<const int, PollContext*>(fd, pctx)).second == false)
	throw DException("Insert");
}

void PollDef::erase(const int fd)
{
    
    unset(fd, PollEvent::POLL_ALL);

    size_t s = ctx_map.size();
    ctx_map.erase(fd);
    if (ctx_map.size() + 1 != s) {
	std::cerr << "Warn: Fd didn't exist in poll set, map size = ";
	std::cerr << ctx_map.size() << "\n";
    }
}

void PollDef::set(const int fd, const PollEnum e)
{
    struct pollfd *pfd = 0;
    
//   if (e == PollEvent::POLL_OUT)
//	std::cerr << "fd " << fd << " set POLL_OUT\n";
    if ((pfd = pfd_find(pfds, n_pfds, fd)) == 0) {
	++n_pfds;
	pfds = reinterpret_cast<struct pollfd *>(realloc(pfds, n_pfds*sizeof(struct pollfd)));
	pfd = &pfds[n_pfds - 1];
	pfd->fd = fd;
	pfd->events = 0;
	pfd->revents = 0;
    }
    pfd->events |= map_event_to_mask(e);
}

void PollDef::unset(const int fd, const PollEnum e)
{
    struct pollfd *pfd = 0;

//    if (e == PollEvent::POLL_OUT)
//	std::cerr << "fd " << fd << " unset POLL_OUT\n";
    if ((pfd = pfd_find(pfds, n_pfds, fd)) != 0) {
	pfd->events &= ~map_event_to_mask(e);
	if (pfd->events == 0) {
	    --n_pfds;
	    memmove(&pfd[0], &pfd[1], (n_pfds - (pfd - pfds))*sizeof(struct pollfd));
	    pfds = reinterpret_cast<struct pollfd *>(realloc(pfds, n_pfds*sizeof(struct pollfd)));
	}
    }
}

int PollDef::poll(const int timeout)
{
    int p_ret;
    int err = 0;
    int p_cnt = 0;
    p_ret = ::poll(pfds, n_pfds, timeout);
    err = errno;
    if (p_ret == -1 && err == EINTR) {
	p_ret = 0;
    } else if (p_ret == -1 && err != EINTR) {
	throw DException("");
    } else {
	for (size_t i = 0; i < n_pfds; i++) {
	    PollEnum e = map_mask_to_event(pfds[i].revents);
	    if (e != PollEvent::POLL_NONE) {
		std::map<const int, PollContext *>::iterator map_i;
		if ((map_i = ctx_map.find(pfds[i].fd)) != ctx_map.end()) {
		    if (map_i->second == 0)
			throw DException("");
		    map_i->second->handle(pfds[i].fd, e);
		    p_cnt++;
		} else {
		    throw FatalException("No ctx for fd found");
		}
	    } else {
		if (pfds[i].revents) {
		    LOG_ERROR("Unhandled poll events");
		    throw FatalException("");
		}
	    }
	}
    }
    // assert(p_ret == p_cnt);
    if (p_ret != p_cnt) {
        LOG_WARN(std::string("p_ret (") 
                 + ::to_string(p_ret) 
                 + ") != p_cnt (" 
                 + ::to_string(p_cnt) 
                 + ")");
    }
    return p_ret;
}

PollDef::PollDef() : n_pfds(0), pfds(0)
{
    
}

PollDef::~PollDef()
{
    ctx_map.clear();
    free(pfds);
}



class FifoPoll : public Poll {
    std::map<const int, std::pair<PollContext *, PollEnum> > ctx_map;
public:
    void insert(const int fd, PollContext *ctx) {
	if (ctx_map.insert(
		std::pair<const int, std::pair<PollContext *, PollEnum> >(
		    fd, 
		    std::pair<PollContext *, PollEnum>(
			ctx, PollEvent::POLL_NONE))).second == false)
	    throw DException("");
    }
    void erase(const int fd) {
	unset(fd, PollEvent::POLL_ALL);
	ctx_map.erase(fd);
    }
    void set(const int fd, const PollEnum e) {
	std::map<const int, std::pair<PollContext *, PollEnum> >::iterator 
	    ctxi = ctx_map.find(fd);
	if (ctxi == ctx_map.end())
	    throw DException("Invalid fd");
	if (Fifo::find(fd) == 0)
	    throw DException("Invalid fd");
	ctxi->second.second |= e;
    }
    void unset(const int fd, const PollEnum e) {
	std::map<const int, std::pair<PollContext *, PollEnum> >::iterator 
	    ctxi = ctx_map.find(fd);
	if (ctxi == ctx_map.end())
	    throw DException("Invalid fd");
	ctxi->second.second &= ~e;
    }
    
    int poll(int tout) {
	int n = 0;
	std::map<const int, std::pair<PollContext *, PollEnum> >::iterator i;
	for (i = ctx_map.begin(); i != ctx_map.end(); ++i) {
	    if (i->second.second & PollEvent::POLL_IN){
		Fifo *fifo = Fifo::find(i->first);
		if (fifo == 0)
		    throw DException("Invalid fd");
		if (fifo->is_empty() == false) {
		    i->second.first->handle(i->first, PollEvent::POLL_IN);
		    n++;
		}
	    }
	    if (i->second.second & PollEvent::POLL_OUT) {
		Fifo *fifo = Fifo::find(i->first);
		if (fifo == 0)
		    throw DException("Invalid fd");
		if (fifo->is_full() == false) {
		    i->second.first->handle(i->first, PollEvent::POLL_OUT);
		    n++;
		}
	    }
	}
	return n;
    }
};


Poll* Poll::create(const char *type)
{
    Poll* ret = 0;
    if (strcasecmp(type, "def") == 0) {
	ret = new PollDef();
    } else if (strcasecmp(type, "fifo") == 0) {
	ret = new FifoPoll();
    } else {
	throw DException("");
    }
    return ret;
}

