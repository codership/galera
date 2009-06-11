#ifndef EVENT_HPP
#define EVENT_HPP

#include <gcomm/common.hpp>
#include <gcomm/exception.hpp>
#include <gcomm/time.hpp>
#include <gcomm/protolay.hpp>
#include <gcomm/string.hpp>

#include <list>
#include <set>
#include <map>

/* Forward declarations */
struct pollfd;



BEGIN_GCOMM_NAMESPACE

struct EventData
{
};

class Event
{
    int event_cause;
    Time time;
    EventData* user_data;
public:
    enum {
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
    
    Event(int cause) : event_cause(cause), user_data(0) {}
    
    Event(int cause, const Time& at) : event_cause(cause), time(at), user_data(0) {}

    Event(int cause, const Time& at, EventData* user_data_) :
        event_cause(cause), time(at), user_data(user_data_) {}

    int get_cause() const {
        return event_cause;
    }

    Time get_time() const {
        return time;
    }

    EventData* get_user_data() const {
        return user_data;
    }

};

class EventContext {
protected:
    EventContext() {}
public:
    virtual ~EventContext() {}
    virtual void handle_event(int, const Event&) = 0;
    virtual void handle_signal(int, int) {}
};



class EventLoop
{
    typedef std::map<const int, EventContext *> CtxMap;
    typedef std::set<int> SigSet;
    typedef std::map<const int, SigSet > SigMap;
    typedef std::multimap<const Time, std::pair<const int, Event> > EventMap;

    CtxMap ctx_map;
    SigMap sig_map;
    EventMap event_map;
    EventMap::iterator active_event;
    size_t n_pfds;
    pollfd *pfds;
    int compute_timeout(const int);
    void handle_queued_events();
    std::list<Protolay*> released;
    bool interrupted;
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

#endif /* EVENT_HPP */
