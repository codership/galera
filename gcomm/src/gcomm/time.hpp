#ifndef _GCOMM_TIME_HPP_
#define _GCOMM_TIME_HPP_

#include <sys/time.h>
#include <ctime>
#include <string>
#include <sstream>
#include <iostream>
#include <limits>

#include <gcomm/common.hpp>
#include <gcomm/exception.hpp>

BEGIN_GCOMM_NAMESPACE

/*!
 * Class for time representation. 
 *
 * 
 */
class Time {
    uint64_t time;
    static const time_t MicroSecond;
    static const time_t MilliSecond;
    static const time_t Second;
    Time(const uint64_t t) : time(t) {}
public:
    Time() : 
        time(0) 
    {
    }

    /*!
     *
     */
    Time(const time_t sec, const time_t usec) :
        time()
    {
	if (uint64_t(sec) > std::numeric_limits<uint64_t>::max()/Second) {
	    std::cerr << sec << " " << std::numeric_limits<time_t>::max()/Second << "\n";
	    throw FatalException("Time value overflow");
	}
	time = uint64_t(sec)*uint64_t(Second) + uint64_t(usec);
    }

    virtual ~Time()
    {
    }

    /*!
     *
     */
    static Time now() {
	timeval tv;
	gettimeofday(&tv, 0);
	return Time(tv.tv_sec, tv.tv_usec);
    }
    
    /*!
     *
     */
    Time operator+(const Time& t) const {
	return time + t.time;
    }
    
    /*!
     *
     */
    bool operator<(const Time& cmp) const {
	return time < cmp.time;
    }

    /*!
     *
     */
    bool operator<=(const Time& cmp) const {
        return time <= cmp.time;
    }

    bool operator>(const Time& cmp) const {
        return time > cmp.time;
    }

    bool operator>=(const Time& cmp) const {
        return time >= cmp.time;
    }

    /*!
     *
     */
    std::string to_string() const {
	std::ostringstream os;
	os.setf(std::ios_base::fixed, std::ios_base::floatfield);
	os << double(time)/double(Second);
	return os.str();
    }

    /*!
     *
     */
    double to_double() const {
	return double(time)/double(Second);
    }
    
    /*!
     *
     */
    time_t get_seconds() const {
	return time/Second;
    }

    /*!
     *
     */
    time_t get_milliseconds() const {
	return time/MilliSecond;
    };

    /*!
     *
     */
    time_t get_microseconds() const {
	return time;
    }
};




/*!
 * Class to represent shortish time periods. Constructor prints warning
 * if given period is longer than 1 hour.
 */
class Period : public Time {
public:
    Period(const Time& t) : Time(t) {
	if (t.get_seconds() > 3600) {
	    std::cerr << "warning: period of " << t.to_string() << " seconds\n";
	}
    }
};

END_GCOMM_NAMESPACE

#endif // _GCOMM_TIME_HPP_
