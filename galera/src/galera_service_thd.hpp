/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#ifndef GALERA_SERVICE_THD_HPP
#define GALERA_SERVICE_THD_HPP

#include <galerautils.h>
#include <galerautils.hpp>
#include <gcs.h>

namespace galera
{
    class ServiceThd
    {
    public:

        ServiceThd (gcs_conn_t* gcs);

        ~ServiceThd ();

        void report_last_committed (gcs_seqno_t seqno);

        void reset(); // reset to initial state before gcs (re)connect

    private:

        gcs_conn_t* const gcs_;
        gu_thread_t       thd_;
        gu::Mutex         mtx_;
        gu::Cond          cond_;

        static const uint32_t A_NONE;

        struct Data
        {
            uint32_t    act_;
            gcs_seqno_t last_committed_;

            Data() : act_(A_NONE), last_committed_(0) {}
        };

        Data data_;

        static void* thd_func (void*);

        ServiceThd (const ServiceThd&);
        ServiceThd& operator= (const ServiceThd&);
    };
}

#endif /* GALERA_SERVICE_THD_HPP */
