#ifndef TIMER_HPP
#define TIMER_HPP

#include <galeracomm/poll.hpp>
#include <galeracomm/time.hpp>

#include <map>


/*!
 * Timer handler interface.
 */ 
class TimerHandler {
    const char *name;
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
    virtual ~TimerHandler() = 0;
};



typedef std::multimap<const Time, TimerHandler *> ExpirationMap;
typedef std::map<const char *, ExpirationMap::iterator> TimerMap;

/*!
 * Timer
 */
class Timer {
    Poll *poll;
    TimerMap timer_map;
    ExpirationMap expiration_map;
public:
    Timer(Poll *);
    ~Timer();
    void set(TimerHandler *, const Period&);
    void unset(const TimerHandler *);
};

#endif // !TIMER_HPP
