/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#include "galera_service_thd.hpp"

const uint32_t galera::ServiceThd::A_NONE = 0;

static const uint32_t A_LAST_COMMITTED = 1 <<  0;
static const uint32_t A_EXIT           = 1 << 31;

void*
galera::ServiceThd::thd_func (void* arg)
{
    galera::ServiceThd* st = reinterpret_cast<galera::ServiceThd*>(arg);
    bool exit = false;

    while (!exit)
    {
        galera::ServiceThd::Data data;

        {
            gu::Lock lock(st->mtx_);

            if (A_NONE == st->data_.act_) lock.wait(st->cond_);

            data = st->data_;
            st->data_.act_ = A_NONE; // clear pending actions
        }

        exit = ((data.act_ & A_EXIT));

        if (!exit)
        {
            if (data.act_ & A_LAST_COMMITTED)
            {
                ssize_t ret;

                if ((ret = st->gcs_.set_last_applied(data.last_committed_)))
                {
                    log_warn << "Failed to report last committed "
                             << data.last_committed_ << ", " << ret
                             << " (" << strerror (-ret) << ')';
                    // @todo: figure out what to do in this case
                }
            }
        }
    }

    return 0;
}

galera::ServiceThd::ServiceThd (GcsI& gcs)
  : gcs_  (gcs),
    thd_  (-1),
    mtx_  (),
    cond_ (),
    data_ ()
{
    gu_thread_create (&thd_, NULL, thd_func, this);
}

galera::ServiceThd::~ServiceThd ()
{
    {
        gu::Lock lock(mtx_);
        data_.act_ |= A_EXIT;
        cond_.signal();
    }

    gu_thread_join(thd_, NULL);
}

void
galera::ServiceThd::report_last_committed(gcs_seqno_t seqno)
{
    gu::Lock lock(mtx_);

    if (data_.last_committed_ < seqno)
    {
        data_.last_committed_ = seqno;

        if (!(data_.act_ & A_LAST_COMMITTED))
        {
            data_.act_ |= A_LAST_COMMITTED;
            cond_.signal();
        }
    }
}

void
galera::ServiceThd::reset()
{
    gu::Lock lock(mtx_);
    data_.act_ = A_NONE;
    data_.last_committed_ = 0;
}
