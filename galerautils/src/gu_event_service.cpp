//
// Copyright (C) 2021 Codership Oy <info@codership.com>
//

#include "gu_event_service.hpp"

#include <cassert>

//
// Event service hooks.
//

namespace gu
{
    std::mutex EventService::mutex;
    size_t     EventService::usage(0);

    EventService* EventService::instance = nullptr;

    int EventService::init_v1(const wsrep_event_service_v1_t* es)
    {
        std::lock_guard<std::mutex> lock(EventService::mutex);
        ++EventService::usage;

        if (EventService::instance)
        {
            assert(0);
            return 0;
        }

        EventService::instance = new EventService(es->context, es->event_cb);
        return 0;
    }

    void EventService::deinit_v1()
    {
        std::lock_guard<std::mutex> lock(EventService::mutex);
        assert(EventService::usage > 0);
        --EventService::usage;

        if (EventService::usage == 0)
        {
            delete EventService::instance;
            EventService::instance = 0;
        }
    }

} /* galera*/
