/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
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
 */

#ifndef GCOMM_PROFILE_HPP
#define GCOMM_PROFILE_HPP

extern "C"
{
#include "gu_time.h"
}

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
    Key(const char* const file,
        const char* const func,
        const int line)
        :
        file_(file),
        func_(func),
        line_(line)
    { }

    bool operator==(const Key& cmp) const
    {
        return (line_ == cmp.line_ &&
                func_ == cmp.func_ &&
                file_ == cmp.file_);
    }

    bool operator<(const Key& cmp) const
    {
        return (line_ < cmp.line_ ||
                (line_ == cmp.line_ && (func_ < cmp.func_ ||
                                        (func_ == cmp.func_ && file_ < cmp.file_))));
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
    const char* const file_;
    const char* const func_;
    const int         line_;
};

inline std::ostream& prof::operator<<(std::ostream& os, const prof::Key& key)
{
    return os << key.file_ << ":" << key.func_ << ":" << key.line_;
}


class prof::Point
{
public:
    Point(const Profile& prof,
          const char* file,
          const char* func,
          const int line);
    ~Point();
private:
    friend class Profile;
    const Profile& prof_;
    const Key key_;
    mutable long long int enter_time_calendar_;
    mutable long long int enter_time_thread_cputime_;
};

/*!
 * Profile class for collecting statistics about profile points.
 */
class prof::Profile
{
    struct PointStats
    {
        PointStats(long long int count                = 0,
                   long long int time_calendar        = 0,
                   long long int time_thread_cputime  = 0) :
            count_              (count              ),
            time_calendar_      (time_calendar      ),
            time_thread_cputime_(time_thread_cputime)
        { }

        PointStats operator+(const PointStats& add) const
        {
            return PointStats(count_               + add.count_,
                              time_calendar_       + add.time_calendar_,
                              time_thread_cputime_ + add.time_thread_cputime_);
        }

        long long int count_;
        long long int time_calendar_;
        long long int time_thread_cputime_;
    };
public:
    /*!
     * Default constructor.
     *
     * @param name_ Name identifying the profile in ostream output.
     */
    Profile(const std::string& name = "profile") :
        name_(name),
        start_time_calendar_(gu_time_calendar()),
        start_time_thread_cputime_(gu_time_thread_cputime()),
        points_()
    { }

    void enter(const Point& point) const
    {
        points_[point.key_].count_++;
        point.enter_time_calendar_ = gu_time_calendar();
        point.enter_time_thread_cputime_ = gu_time_thread_cputime();
    }

    void leave(const Point& point) const
    {
        long long int t_cal(gu_time_calendar());
        long long int t_thdcpu(gu_time_thread_cputime());
        points_[point.key_].time_calendar_ +=
            (t_cal - point.enter_time_calendar_);
        points_[point.key_].time_thread_cputime_ +=
            (t_thdcpu - point.enter_time_thread_cputime_);
    }

    void clear() { points_.clear(); }

    friend std::ostream& operator<<(std::ostream&, const Profile&);

    typedef std::map<Key, PointStats> Map;
    std::string const name_;
    long long int const start_time_calendar_;
    long long int const start_time_thread_cputime_;
    mutable Map points_;
};

inline prof::Point::Point(const Profile& prof,
                          const char*    file,
                          const char*    func,
                          const int      line) :
    prof_(prof),
    key_(file, func, line),
    enter_time_calendar_(),
    enter_time_thread_cputime_()
{
    prof_.enter(*this);
}

inline prof::Point::~Point()
{
    prof_.leave(*this);
}


/*!
 * Ostream operator for Profile class.
 */
inline std::ostream& prof::operator<<(std::ostream& os, const Profile& prof)
{
    Profile::PointStats cumul;

    char prev_fill(os.fill());
    os.fill(' ');
    os << "\nprofile name: " << prof.name_;


    os << std::left << std::fixed << std::setprecision(3);
    os << "\n\n";
    os << std::setw(40) << "point";
    os << std::setw(10) << "count";
    os << std::setw(10) << "calendar";
    os << std::setw(10) << "cpu";
    os << "\n"
       << std::setfill('-') << std::setw(70) << ""
       << std::setfill(' ') << "\n";
    for (Profile::Map::const_iterator i = prof.points_.begin();
         i != prof.points_.end(); ++i)
    {
        os << std::setw(40) << std::left << i->first.to_string();
        os << std::right;
        os << std::setw(10) << i->second.count_;
        os << std::setw(10) << double(i->second.time_calendar_)*1.e-9;
        os << std::setw(10) << double(i->second.time_thread_cputime_)*1.e-9;
        os << std::left;
        os << "\n";
        cumul = cumul + i->second;
    }

    os << "\ntot count         : " << cumul.count_;
    os << "\ntot calendar time : " << double(cumul.time_calendar_)*1.e-9;
    os << "\ntot thread cputime: " << double(cumul.time_thread_cputime_)*1.e-9;
    os << "\ntot ct since ctor : "
       << double(gu::datetime::Date::now().get_utc()
                 - prof.start_time_calendar_)*1.e-9;

    os.fill(prev_fill);
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
