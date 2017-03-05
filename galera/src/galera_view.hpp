//
// Copyright (C) 2015 Codership Oy <info@codership.com>
//

/*! @file galera_view.hpp
 *
 * Helper class and methods for manipulating views in galera code.
 */


#ifndef GALERA_VIEW_HPP
#define GALERA_VIEW_HPP

#include "wsrep_api.h" // for wsrep_view_info_t
#include "gu_uuid.hpp"
#include <set>

static inline bool operator<(const wsrep_uuid_t& lhs, const wsrep_uuid_t& rhs)
{
    return (memcmp(lhs.data, rhs.data, sizeof(lhs.data)) < 0);
}


namespace galera
{
    class View
    {
    public:
        class UUIDCmp
        {
        public:
            bool operator()(const wsrep_uuid_t& lhs,
                            const wsrep_uuid_t& rhs) const
            {
                return (lhs < rhs);
            }
        };
        // Convenience typedef for member set
        typedef std::set<wsrep_uuid_t, UUIDCmp> MembSet;
        // Default constructor
        View();
        // Construct View from wsrep_view_info_t
        View(const wsrep_view_info_t&);
        // Destructor
        ~View();
        // Return true if the members of the view are subset of
        // other MembSet.
        bool subset_of(const MembSet& other) const;
    private:
        MembSet      members_; // members set
    };
}

#endif // GALERA_VIEW_HPP
