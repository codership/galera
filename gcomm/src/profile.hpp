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

#include "gu_datetime.hpp"
#include "gu_lock.hpp"

#include <map>
#include <ostream>
#include <cstring>

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
    mutable gu::datetime::Date enter_time;
};

/*!
 * Profile class for collecting statistics about profile points.
 */
class prof::Profile
{
public:
    /*!
     * Default constructor.
     *
     * @param name_ Name identifying the profile in ostream output.
     */
    Profile(const std::string& name_ = "profile") : 
        name(name_),
        start_time(gu::datetime::Date::now()),
        points(), 
        c_time(0LL),
        mutex()
    { }
    
    void enter(const Point& point) const
    { 
        // gu::Lock lock(mutex);
        points[point.key].first++; 
        point.enter_time = gu::datetime::Date::now();
    }
    
    void leave(const Point& point) const
    { 
        long long int t(gu::datetime::Date::now().get_utc() - 
                        point.enter_time.get_utc());
        // gu::Lock lock(mutex);
        points[point.key].second += t; 
        c_time += t;
    }
    
    void clear() { c_time = 0; points.clear(); }
    
    friend std::ostream& operator<<(std::ostream&, const Profile&);
    
    typedef std::map<Key, std::pair<long long int, long long int> > Map;
    std::string const name;
    gu::datetime::Date const start_time;
    mutable Map points;
    mutable long long int c_time;
    mutable gu::Mutex mutex;
};

inline prof::Point::Point(const Profile& prof_, 
                          const char* file_, 
                          const char* func_, 
                          const int line_) :
    prof(prof_),
    key(file_, func_, line_),
    enter_time()
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
    os << "\n\t" << prof.name << ":";
    os << "\n\tcumulative time: " << double(prof.c_time)/gu::datetime::Sec;
    os << "\n\toverhead: " 
       << double(prof.c_time)/double(gu::datetime::Date::now().get_utc() -
                                     prof.start_time.get_utc());
    for (Profile::Map::const_iterator i = prof.points.begin(); 
         i != prof.points.end(); ++i)
    {
        os << "\n\t" << i->first << ": " 
           << i->second.first     << " "
           << double(i->second.second)/gu::datetime::Sec << " "
           << double(i->second.second)/double(prof.c_time);
    }
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
