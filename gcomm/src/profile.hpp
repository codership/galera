/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id:$
 */

/*!
 * @file profile.hpp
 *
 * @brief Lightweight profiling utility. 
 *
 * Profiling utility suitable for getting runtime code profile information
 * with minimal overhead. Macros profile_enter() and profile_leave()
 * can be inserted around the code and will be expanded to profiling 
 * code if GCOMM_PROFILE is defined.
 *
 * Example usage:
 * @code
 *
 * Profile prof("prof");
 *
 * void func()
 * {
 *     if (is_true())
 *     {
 *         profile_enter(prof);  // This is line 227
 *         // Do something
 *         // ...
 *         profile_leave(prof);
 *     }
 *     else
 *     {
 *         profile_enter(prof);  // This is line 250
 *         // Do something else
 *         // ...
 *         profile_leave(prof);
 *     }
 * }
 *
 * // Somewhere else in your code
 * log_info << prof;
 * @endcode
 *
 * Output will be something along the lines
 *
 * @code
 * prof:
 * cumulative_time: 0.0366198
 * file.cpp:func:227: 6 0.0328768
 * file.cpp:func:250: 5 0.00374298
 * @endcode
 *
 * From the output you can read that branch starting from line 227 was
 * entered 6 times and time spent in the branch was around 0.0329 seconds,
 * the other branch was entered 5 times and time spent there was around
 * 0.0037 seconds.
 */

#ifndef GCOMM_PROFILE_HPP
#define GCOMM_PROFILE_HPP

#include "gu_time.h"

#include <map>
#include <ostream>

namespace prof
{
    
    // Forward declarations
    class Key;
    class Point;
    class Profile;
    std::ostream& operator<<(std::ostream&, const Key&);
    std::ostream& operator<<(std::ostream&, const Profile&);
}

/*!
 * Profile key storing human readable point description <file>:<func>:<line>
 * and entry time.
 */

class prof::Key
{
public:
    Key(const char* const file_, 
        const char* const func_, 
        const int line_) :
        file(file_),
        func(func_),
        line(line_)
    { }

    bool operator==(const Key& cmp) const
    {
        return (line == cmp.line && 
                func == cmp.func &&
                file == cmp.file);
    }
    
    bool operator<(const Key& cmp) const
    {
        return (line < cmp.line ||
                (line == cmp.line && (func < cmp.func ||
                                      (func == cmp.func && file < cmp.file))));
    }
    std::string to_string() const
    {
        std::ostringstream os;
        os << *this;
        return os.str();
    }
private:
    friend class Point;
    friend class Profile;
    friend std::ostream& operator<<(std::ostream& os, const Key&);
    const char* const file;
    const char* const func;
    const int         line;
};

inline std::ostream& prof::operator<<(std::ostream& os, const prof::Key& key)
{
    return os << key.file << ":" << key.func << ":" << key.line;
} 


class prof::Point
{
public:
    Point(const Profile& prof_, 
          const char* file_, 
          const char* func_, 
          const int line_);
    ~Point();
private:
    friend class Profile;
    const Profile& prof;
    const Key key;
    mutable long long int enter_time_calendar;
    mutable long long int enter_time_thread_cputime;
};

/*!
 * Profile class for collecting statistics about profile points.
 */
class prof::Profile
{
    struct PointStats
    {
        PointStats(long long int count_               = 0,
                   long long int time_calendar_       = 0,
                   long long int time_thread_cputime_ = 0) : 
            count               (count_              ), 
            time_calendar       (time_calendar_      ), 
            time_thread_cputime (time_thread_cputime_) 
        { }

        PointStats operator+(const PointStats& add) const
        {
            return PointStats(count               + add.count,
                              time_calendar       + add.time_calendar,
                              time_thread_cputime + add.time_thread_cputime);
        }
        
        long long int count;
        long long int time_calendar;
        long long int time_thread_cputime;
    };
public:
    /*!
     * Default constructor.
     *
     * @param name_ Name identifying the profile in ostream output.
     */
    Profile(const std::string& name_ = "profile") : 
        name(name_),
        start_time_calendar(gu_time_calendar()),
        start_time_thread_cputime(gu_time_thread_cputime()),
        points()
    { }
    
    void enter(const Point& point) const
    { 
        points[point.key].count++; 
        point.enter_time_calendar = gu_time_calendar();
        point.enter_time_thread_cputime = gu_time_thread_cputime();
    }
    
    void leave(const Point& point) const
    { 
        long long int t_cal(gu_time_calendar());
        long long int t_thdcpu(gu_time_thread_cputime());
        points[point.key].time_calendar += (t_cal - point.enter_time_calendar);
        points[point.key].time_thread_cputime += (t_thdcpu - point.enter_time_thread_cputime);
    }
    
    void clear() { points.clear(); }
    
    friend std::ostream& operator<<(std::ostream&, const Profile&);
    
    typedef std::map<Key, PointStats> Map;
    std::string const name;
    long long int const start_time_calendar;
    long long int const start_time_thread_cputime;
    mutable Map points;
};

inline prof::Point::Point(const Profile& prof_, 
                          const char* file_, 
                          const char* func_, 
                          const int line_) :
    prof(prof_),
    key(file_, func_, line_),
    enter_time_calendar(),
    enter_time_thread_cputime()
{ 
    prof.enter(*this); 
}

inline prof::Point::~Point() 
{ 
    prof.leave(*this); 
}


/*!
 * Ostream operator for Profile class.
 */
inline std::ostream& prof::operator<<(std::ostream& os, const Profile& prof)
{

    Profile::PointStats cumul;

    os << "\nprofile name: " << prof.name;
    

    os << std::left << std::fixed << std::setprecision(7);
    os << "\n\n";
    os << std::setw(40) << "point";
    os << std::setw(10) << "count";
    os << std::setw(10) << "calendar";
    os << std::setw(10) << "cpu";
    os << "\n" 
       << std::setfill('-') << std::setw(70) << "" 
       << std::setfill(' ') << "\n";
    for (Profile::Map::const_iterator i = prof.points.begin(); 
         i != prof.points.end(); ++i)
    {
        os << std::setw(40) << i->first.to_string();
        os << std::setw(10) << i->second.count;
        os << std::setw(10) << double(i->second.time_calendar)*1.e-9;
        os << std::setw(10) << double(i->second.time_thread_cputime)*1.e-9;
        os << "\n";
        cumul = cumul + i->second;
    }
    
    os << "\ntot count         : " << cumul.count;
    os << "\ntot calendar time : " << double(cumul.time_calendar)*1.e-9;
    os << "\ntot thread cputime: " << double(cumul.time_thread_cputime)*1.e-9;
    
    return os;
}


/*
 * Convenience macros for defining profile entry and leave points. 
 * If GCOMM_PROFILE is undefined, these macros expand to no-op.
 */
#ifdef GCOMM_PROFILE
#define profile_enter(__p) do { \
    const prof::Point __point((__p), __FILE__,  __FUNCTION__, __LINE__); \

#define profile_leave(__p) \
      } while (0)
#else
#define profile_enter(__p)
#define profile_leave(__p)
#endif // GCOMM_PROFILE

#endif // GCOMM_PROFILE_HPP
