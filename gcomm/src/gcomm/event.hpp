#ifndef _GCOMM_EVENT_HPP_
#define _GCOMM_EVENT_HPP_

#include <gcomm/common.hpp>
#include <gcomm/exception.hpp>
#include <gcomm/time.hpp>
#include <gcomm/protolay.hpp>

#include <list>
#include <set>
#include <map>
#include <vector>

#include <poll.h>

BEGIN_GCOMM_NAMESPACE

struct EventData {};

class Event
{
    int        event_cause;
    Time       time;
    EventData* user_data;

    void operator=(const Event&);

public:

    enum
    {
	E_NONE   = 0,
	E_IN     = 1 << 0,
	E_OUT    = 1 << 1,
	E_ERR    = 1 << 2,
	E_HUP    = 1 << 3,
	E_INVAL  = 1 << 4,
	E_TIMED  = 1 << 5,
        E_SIGNAL = 1 << 6,
        E_USER   = 1 << 7,
	E_ALL = E_IN | E_OUT | E_ERR | E_HUP | E_INVAL | E_TIMED | E_SIGNAL | E_USER
    };

    Event(const Event& e)
        :
        event_cause (e.event_cause),
        time        (e.time),
        user_data   (e.user_data)

    {}
    
    Event(int cause) : event_cause(cause), time(Time::now()), user_data(0) {}
    
    Event(int cause, const Time& at, EventData* user_data_ = 0)
        :
        event_cause (cause), 
        time        (at), 
        user_data   (user_data_) 
    {}

    int get_cause() const { return event_cause; }

    Time get_time() const { return time; }

    EventData* get_user_data() const { return user_data; }
};


class EventContext
{
protected:

    EventContext() {}

public:

    virtual ~EventContext() {}
    virtual void handle_event(int, const Event&) = 0;
    virtual void handle_signal(int, int) {}
};


class EventLoop
{
public:
    typedef std::map<const int, EventContext *> CtxMap;
    typedef std::set<int> SigSet;
    typedef std::map<const int, SigSet > SigMap;
    typedef std::multimap<const Time, std::pair<const int, Event> > EventMap;

    CtxMap ctx_map;
    SigMap sig_map;
    EventMap event_map;
    EventMap::iterator active_event;
    bool pfds_changed;
    std::vector<pollfd> pfds;
    int compute_timeout(const int);
    void handle_queued_events();
    std::list<Protolay*> released;
    size_t unset_fds;
    bool interrupted;

private:
    EventLoop(const EventLoop&);
    void operator=(const EventLoop&);
public:

    EventLoop();
    ~EventLoop();

    void release_protolay(Protolay*);
    void insert(int fd, EventContext *pctx);
    void erase(int fd);
    void set(int fd, int);
    void unset(int fd, int);
    void set_signal(int fd, int signo);
    void unset_signal(int fd, int signo);
    void queue_event(int fd, const Event&);
    int poll(int timeout);
    void interrupt()
    {
        interrupted = true;
    }
    bool is_interrupted() const
    {
        return interrupted;
    }
};

END_GCOMM_NAMESPACE

#endif /* _GCOMM_EVENT_HPP_ */
