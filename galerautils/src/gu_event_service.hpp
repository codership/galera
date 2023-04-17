//
// Copyright (C) 2021 Codership Oy <info@codership.com>
//

/**
 * Event service class
 */

#ifndef GALERA_EVENT_SERVICE_HPP
#define GALERA_EVENT_SERVICE_HPP

#include "wsrep_event_service.h"

#include <string>
#include <mutex>

namespace gu
{
    class EventService
    {
    public:
        static int  init_v1(const wsrep_event_service_v1_t*);
        static void deinit_v1();

        static void callback(const std::string& name, const std::string& value)
        {
            std::lock_guard<std::mutex> lock(EventService::mutex);

            if (instance && instance->cb_)
            {
                instance->cb_(instance->ctx_, name.c_str(), value.c_str());
            }
        }

    private:
        wsrep_event_context_t* const ctx_;
        wsrep_event_cb_t       const cb_;

        static std::mutex    mutex;
        static size_t        usage;
        static EventService* instance;

        EventService(wsrep_event_context_t* ctx, wsrep_event_cb_t cb)
            : ctx_(ctx), cb_(cb)
        {}
        ~EventService() {}

        EventService(const EventService&);
        EventService& operator =(EventService);
    };

} /* galera */

#endif /* GALERA_EVENT_SERVICE_HPP */
