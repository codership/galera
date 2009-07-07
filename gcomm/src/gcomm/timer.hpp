#ifndef TIMER_HPP
#define TIMER_HPP

#include <gcomm/common.hpp>
#include <gcomm/event.hpp>
#include <gcomm/time.hpp>

#include <map>

BEGIN_GCOMM_NAMESPACE

/*!
 * Timer handler interface.
 */ 
class TimerHandler {
    const char* name;
    TimerHandler(const TimerHandler&);
    void operator=(const TimerHandler&);
public:
    /*!
     *
     */
    TimerHandler(const char *n) : name(n) {}

    /*!
     *
     */
    const char *get_name() const {
	return name;
    }

    /*!
     * This method is called when timer expires. 
     */
    virtual void handle() = 0;
    
    /*!
     * Virtual destructor.
     */
    virtual ~TimerHandler() {}
};



typedef std::multimap<const Time, TimerHandler*> ExpirationMap;
typedef std::map<const char *, ExpirationMap::iterator> TimerMap;

/*!
 * Timer
 */
class Timer : public EventContext {
    int fd;
    EventLoop *event_loop;
    TimerMap timer_map;
    ExpirationMap expiration_map;
    Timer(const Timer&);
    void operator=(const Timer&);
public:
    Timer(EventLoop *);
    ~Timer();
    void set(TimerHandler*, const Period&);
    void unset(const TimerHandler*);
    bool is_set(const TimerHandler*);
    void handle_event(int, const Event&);
};

END_GCOMM_NAMESPACE

#endif // !TIMER_HPP
