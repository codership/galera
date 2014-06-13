// Copyright (C) 2014 Codership Oy <info@codership.com>

//!
// @file
// Common definitions for status variables
//


#ifndef GU_STATUS_HPP
#define GU_STATUS_HPP

#include "gu_exception.hpp"
#include <map>
#include <string>

namespace gu
{

    class Status
    {
    public:
        typedef std::map<std::string, std::string> VarMap;
        typedef VarMap::iterator                   iterator;
        typedef VarMap::const_iterator             const_iterator;

        Status() : vars_() { }
        void insert(const std::string& key, const std::string& val)
        {
            vars_.insert(std::make_pair(key, val));
        }
        void erase(const std::string& key)
        {
            vars_.erase(key);
        }

        const_iterator begin() { return vars_.begin(); }
        const_iterator end()   { return vars_.end(); }
        const std::string& find(const std::string& key) const
        {
            VarMap::const_iterator i(vars_.find(key));
if (i == vars_.end()) throw gu::NotFound();
            return i->second;
        }

    private:
        VarMap vars_;
    };

}



#endif // !GU_STATUS_HPP
