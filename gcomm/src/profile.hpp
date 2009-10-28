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

#ifdef GCOMM_PROFILE
#include "gcomm/map.hpp"
#include "gcomm/time.hpp"
#include "gu_utils.hpp"
#endif // GCOMM_PROFILE

#include <ostream>

namespace gcomm
{
    // Forward declarations
    class ProfilePoint;
    class Profile;
    std::ostream& operator<<(std::ostream&, const std::pair<long long int, long long int>&);
    std::ostream& operator<<(std::ostream&, const Profile&);
}

/*!
 * Profile point storing human readable point description <file>:<func>:<line>
 * and entry time.
 */
class gcomm::ProfilePoint
{
public:
    ProfilePoint(Profile& prof_, const char* file, const char* func, const int line);
    ~ProfilePoint();
    const std::string& get_point() const { return point; }
    const gcomm::Time& get_enter_time() const { return enter_time; }
private:
    Profile& prof;
    std::string point;
    const gcomm::Time enter_time;
};

/*!
 * Profile class for collecting statistics about profile points.
 */
class gcomm::Profile
{
public:
    /*!
     * Default constructor.
     *
     * @param name_ Name identifying the profile in ostream output.
     */
    Profile(const std::string& name_ = "profile") : 
        name(name_),
        points(), 
        c_time(0LL) 
    { }
    
private:
    friend class ProfilePoint;
    
    void enter(const ProfilePoint& point) 
    { 
        points[point.get_point()].first++; 
    }
    
    void leave(const ProfilePoint& point) 
    { 
        long long int t(Time::now().get_utc() - point.get_enter_time().get_utc());
        points[point.get_point()].second += t; 
        c_time += t;
    }
    
    void clear() { c_time = 0; points.clear(); }
    
    friend std::ostream& operator<<(std::ostream&, const Profile&);
    
    class Map : 
        public gcomm::Map<std::string, std::pair<long long int, long long int> > { };
    std::string const name;
    Map points;
    long long int c_time;
    
};


inline gcomm::ProfilePoint::ProfilePoint(Profile& prof_, 
                                         const char* file, 
                                         const char* func, 
                                         const int line) : 
    prof(prof_),
    point(), 
    enter_time(Time::now())
{
    std::ostringstream os;
    os << file << ":" << func << ":" << line;
    point = os.str();
    prof.enter(*this);
}

inline gcomm::ProfilePoint::~ProfilePoint()
{ 
    prof.leave(*this); 
}


/*!
 * Ostream operator for Profile class.
 */
inline std::ostream& gcomm::operator<<(std::ostream& os, const Profile& prof)
{
    os << "\n\t" << prof.name << ":";
    os << "\n\tcumulative time: " << double(prof.c_time)/gu::datetime::Sec;
    for (Profile::Map::const_iterator i = prof.points.begin(); 
         i != prof.points.end(); ++i)
    {
        os << "\n\t" << Profile::Map::get_key(i) << ": " 
           << Profile::Map::get_value(i).first << " "
           << double(Profile::Map::get_value(i).second)/gu::datetime::Sec ;
    }
    return os;
}


/*
 * Convenience macros for defining profile entry and leave points. 
 * If GCOMM_PROFILE is undefined, these macros expand to no-op.
 */
#ifdef GCOMM_PROFILE
#define profile_enter(__p) do { \
    const gcomm::ProfilePoint __point((__p), __FILE__,  __FUNCTION__, __LINE__); \

#define profile_leave(__p) \
      } while (0)
#else
#define profile_enter(__p)
#define profile_leave(__p)
#endif // GCOMM_PROFILE

#endif // GCOMM_PROFILE_HPP
