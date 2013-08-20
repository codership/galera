/*
 * Copyright (C) 2010-2013 Codership Oy <info@codership.com>
 */

#ifndef GALERA_SERVICE_THD_HPP
#define GALERA_SERVICE_THD_HPP

#include "gcs.hpp"
#include <GCache.hpp>

#include <galerautils.h>
#include <galerautils.hpp>

namespace galera
{
    class ServiceThd
    {
    public:

        ServiceThd (GcsI& gcs, gcache::GCache& gcache);

        ~ServiceThd ();

        /*! flush all ongoing operations (before processing CC) */
        void flush ();

        /*! reset to initial state before gcs (re)connect */
        void reset();

        /* !!!
         * The following methods must be invoked only within a monitor,
         * so that monitors drain during CC ensures that no outdated
         * actions are scheduled with the service thread after that.
         * !!! */

        /*! schedule seqno to be reported as last committed */
        void report_last_committed (gcs_seqno_t seqno);

        /*! release write sets up to and including seqno */
        void release_seqno (gcs_seqno_t seqno);

    private:

        static const uint32_t A_NONE;

        struct Data
        {
            gcs_seqno_t last_committed_;
            gcs_seqno_t release_seqno_;
            uint32_t    act_;

            Data() :
                last_committed_(0),
                release_seqno_ (0),
                act_           (A_NONE)
            {}
        };

        gcache::GCache& gcache_;
        GcsI&           gcs_;
        gu_thread_t     thd_;
        gu::Mutex       mtx_;
        gu::Cond        cond_;  // service request condition
        gu::Cond        flush_; // flush condition
        Data            data_;

        static void* thd_func (void*);

        ServiceThd (const ServiceThd&);
        ServiceThd& operator= (const ServiceThd&);
    };
}

#endif /* GALERA_SERVICE_THD_HPP */
