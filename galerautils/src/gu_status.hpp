// Copyright (C) 2014 Codership Oy <info@codership.com>

//!
// @file
// Common class for gathering Galera wide status. The class is simple
// string based key-value store.
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

        const_iterator begin() { return vars_.begin(); }

        const_iterator end()   { return vars_.end(); }

        size_t size() const { return vars_.size(); }

    private:
        VarMap vars_;
    };
}



#endif // !GU_STATUS_HPP
