#ifndef POLL_HPP
#define POLL_HPP

#include <gcomm/exception.hpp>
#include <string>



class PollContext;
typedef short int PollEnum;

namespace PollEvent {
    enum {
	POLL_NONE = 0,
	POLL_IN = 1 << 0,
	POLL_OUT = 1 << 1,
	POLL_ERR = 1 << 2,
	POLL_HUP = 1 << 3,
	POLL_INVAL = 1 << 4,
	POLL_TIMED = 1 << 5,
	POLL_ALL = POLL_IN | POLL_OUT | POLL_ERR | POLL_HUP | POLL_INVAL | POLL_TIMED
    };
}




class Poll {
protected:
    Poll() {}
public:
    static Poll* create(const char *);
    virtual ~Poll() {}
    virtual void insert(const int fd, PollContext *pctx) = 0;
    virtual void erase(const int fd) = 0;
    virtual void set(const int fd, const PollEnum e) = 0;
    virtual void unset(const int fd, const PollEnum e) = 0;
    virtual int poll(const int timeout) = 0;
};

class PollContext {
    PollEnum e;
protected:
    PollContext() : e(0) {}
public:
    virtual ~PollContext() {}
    virtual void handle(const int, const PollEnum e) = 0;
};




#endif /* POLL_HPP */
