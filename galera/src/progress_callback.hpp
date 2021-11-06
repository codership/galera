//
// Copyright (C) 2021 Codership Oy <info@codership.com>
//

#ifndef GALERA_PROGRESS_CALLBACK_HPP
#define GALERA_PROGRESS_CALLBACK_HPP

#include "gu_progress.hpp" // gu::Progress::Callback
#include "event_service.hpp"
#include "wsrep_api.h"

#include <string>
#include <ostream>

namespace galera
{
    template <typename T>
    class ProgressCallback : public gu::Progress<T>::Callback
    {
    public:
        ProgressCallback(wsrep_member_status_t from,
                         wsrep_member_status_t to)
            : from_(from), to_(to)
        {}

        void operator()(T total, T done)
        {
            static std::string const event_name("progress");

            std::ostringstream os;
            os << "{ \"from\": "  << from_
               << ", \"to\": "    << to_
               << ", \"total\": " << total
               << ", \"done\": "  << done
               << ", \"undefined\": -1 }";

            EventService::callback(event_name, os.str());
        }

    private:
        wsrep_member_status_t const from_;
        wsrep_member_status_t const to_;

    }; /* ProgressCallback */

} /* galera */

#endif /* GALERA_PROGRESS_CALLBACK_HPP */
