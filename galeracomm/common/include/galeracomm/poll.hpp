#ifndef POLL_HPP
#define POLL_HPP

//#include <galeracomm/exception.hpp>
#include <string>
#include <sys/time.h>
#include <time.h>

#include <galerautils.hpp>

class PollContext;
typedef short int PollEnum;

namespace PollEvent {
    enum {
	POLL_NONE  = 0,
	POLL_IN    = 1 << 0,
	POLL_OUT   = 1 << 1,
	POLL_ERR   = 1 << 2,
	POLL_HUP   = 1 << 3,
	POLL_INVAL = 1 << 4,
	POLL_TIMED = 1 << 5,
	POLL_ALL   = POLL_IN | POLL_OUT | POLL_ERR | POLL_HUP | POLL_INVAL | POLL_TIMED
    };
}


class Poll {
protected:
    Poll() {}
public:
    static int const DEFAULT_KA_TIMEOUT;
    static int const DEFAULT_KA_INTERVAL;
    static Poll* create(const char *);
    virtual ~Poll() {}
    virtual void insert(const int fd, PollContext *pctx) = 0;
    virtual void erase(const int fd) = 0;
    virtual void set(const int fd, const PollEnum e) = 0;
    virtual void unset(const int fd, const PollEnum e) = 0;
    /*                           ms */
    virtual int poll(const int timeout = std::numeric_limits<int>::max()) = 0;
};

class PollContext
{
public:

    static long long get_timestamp()
    {
        return (gu_time_monotonic() / 1000000LL);
    }

    virtual ~PollContext() {}                                 /* ms */
    virtual void handle(const int, const PollEnum e, long long tstamp) = 0;

protected:

    PollContext() : e(0) {}

private:

    PollEnum e;
};


#endif /* POLL_HPP */
