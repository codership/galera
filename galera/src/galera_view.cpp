//
// Copyright (C) 2015 Codership Oy <info@codership.com>
//


#include "galera_view.hpp"

#include <algorithm>

galera::View::View()
    :
    members_()
{ }

galera::View::View(const wsrep_view_info_t& view_info)
    :
    members_()
{
    for (int i(0); i < view_info.memb_num; ++i)
    {
        members_.insert(view_info.members[i].id);
    }
}

galera::View::~View()
{ }


bool galera::View::subset_of(const MembSet& mset) const
{
    return std::includes(mset.begin(), mset.end(),
                         members_.begin(), members_.end(), UUIDCmp());
}
