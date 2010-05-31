/* Copyright (C) 2010 Codership Oy */
#ifndef GALERA_APPLY_MONITOR_HPP
#define GALERA_APPLY_MONITOR_HPP

#include "trx_handle.hpp"
#include "gu_lock.hpp"

#include <vector>

namespace galera
{
    template <class C>
    class Monitor
    {
    private:

        struct Applier
        {
            Applier() : trx_(0), cond_(), state_(S_IDLE) { }

            Applier(const Applier& other)
                :
                trx_(other.trx_),
                cond_(other.cond_),
                state_(other.state_)
            { }

            const TrxHandle* trx_;
            gu::Cond cond_;

            enum State
            {
                S_IDLE,     // Slot is free
                S_WAITING,  // Waiting to enter applying critical section
                S_CANCELED,
                S_APPLYING, // Applying
                S_FINISHED  // Finished
            } state_;

        private:

            void operator=(const Applier&);
        };

        static const size_t appliers_size_ = (1 << 14);
        static const size_t appliers_mask_ = appliers_size_ - 1;

    public:

        enum Mode
        {
            M_BYPASS,
            M_NORMAL
        };

        Monitor()
            :
            condition_(),
            mode_(M_NORMAL),
            mutex_(),
            pre_enter_cond_(),
            last_entered_(-1),
            last_left_(-1),
            appliers_(appliers_size_),
            entered_(0),
            oooe_(0),
            oool_(0)
        { }

        ~Monitor()
        {
            if (entered_ > 0)
            {
                log_info << "apply mon: entered " << entered_
                         << " oooe fraction " << double(oooe_)/entered_
                         << " oool fraction " << double(oool_)/entered_;
            }
            else
            {
                log_info << "apply mon: entered 0";
            }
        }

        void assign_mode(Mode mode) { mode_ = mode; }

        void assign_initial_position(wsrep_seqno_t seqno)
        {
            assert(last_entered_ == last_left_);
            log_info << "initial position " << seqno;
            last_entered_ = last_left_ = seqno;
        }

        int enter(TrxHandle* trx)
        {
            if (mode_ == M_BYPASS) return 0;

            wsrep_seqno_t trx_seqno(trx->get_global_seqno());
            size_t        idx(indexof(trx_seqno));
            gu::Lock      lock(mutex_);

            pre_enter(trx, lock, idx);

            if (appliers_[idx].state_ ==  Applier::S_CANCELED)
            {
                return -ECANCELED;
            }

            appliers_[idx].state_ = Applier::S_WAITING;
            appliers_[idx].trx_   = trx;

            while (may_enter(trx) == false)
            {
                trx->unlock();
                lock.wait(appliers_[idx].cond_);
                trx->lock();
                if (appliers_[idx].state_ == Applier::S_CANCELED)
                {
                    return -ECANCELED;
                }
            }

            appliers_[idx].state_ = Applier::S_APPLYING;

            ++entered_;
            oooe_ += (last_left_ + 1 < trx_seqno);

            return 0;
        }

        void leave(const TrxHandle* trx)
        {
            if (mode_ == M_BYPASS) return;

            wsrep_seqno_t trx_seqno = trx->get_global_seqno();
            size_t   idx(indexof(trx_seqno));
            gu::Lock lock(mutex_);

            assert(appliers_[idx].state_ == Applier::S_APPLYING ||
                   appliers_[idx].state_ == Applier::S_CANCELED);

            appliers_[idx].state_ = Applier::S_FINISHED;

            assert(appliers_[indexof(last_left_)].state_ == Applier::S_IDLE);

            // Note: We need two scans here
            // 1) Update last left
            // 2) Check waiters that may enter due to updated last left
            const wsrep_seqno_t prev_last_left(last_left_);
            size_t              n_waiters(0);

            for (wsrep_seqno_t i = last_left_ + 1; i <= last_entered_; ++i)
            {
                Applier& a(appliers_[indexof(i)]);
                switch (a.state_)
                {
                case Applier::S_FINISHED:
                    if (last_left_ + 1 == i)
                    {
                        a.state_   = Applier::S_IDLE;
                        last_left_ = i;
                    }
                    break;
                case Applier::S_WAITING:
                    assert(a.trx_ != 0);
                    ++n_waiters;
                    break;
                case Applier::S_CANCELED:
                case Applier::S_APPLYING:
                    break;
                default:
                    gu_throw_fatal << "invalid state " << a.state_;
                }
            }

            for (wsrep_seqno_t i = prev_last_left + 1;
                 n_waiters > 0 && i <= last_entered_; ++i)
            {
                Applier& a(appliers_[indexof(i)]);
                if (a.state_ == Applier::S_WAITING)
                {
                    if (may_enter(a.trx_) == true)
                    {
                        a.cond_.signal();
                    }
                    --n_waiters;
                }
            }
            appliers_[idx].trx_ = 0;

            assert(n_waiters == 0);
            assert((last_left_ >= trx_seqno &&
                    appliers_[idx].state_ == Applier::S_IDLE) ||
                   appliers_[idx].state_ == Applier::S_FINISHED);
            assert(last_left_ != last_entered_ ||
                   appliers_[indexof(last_left_)].state_ == Applier::S_IDLE);

            oool_ += (last_left_ > trx_seqno);

            // log_info << "apply monitor leave " << trx_seqno;
        }

        void self_cancel(TrxHandle* trx)
        {
            if (mode_ == M_BYPASS) return;

            size_t   idx(indexof(trx->get_global_seqno()));
            gu::Lock lock(mutex_);

            pre_enter(trx, lock, idx);
            appliers_[idx].state_ = Applier::S_CANCELED;
        }

        void cancel(const TrxHandle* trx)
        {
            if (mode_ == M_BYPASS) return;

            size_t   idx(indexof(trx->get_global_seqno()));
            gu::Lock lock(mutex_);

            pre_enter(0, lock, idx);

            if (appliers_[idx].state_ <= Applier::S_WAITING)
            {
                appliers_[idx].state_ = Applier::S_CANCELED;
                appliers_[idx].cond_.signal();
            }
            else
            {
                log_debug << "cancel, applier state " << appliers_[idx].state_;
            }
        }

        std::pair<double, double> get_ooo_stats() const
        {
            gu::Lock lock(mutex_);
            double oooe(entered_ == 0 ? .0 : double(oooe_)/entered_);
            double oool(entered_ == 0 ? .0 : double(oool_)/entered_);
            return std::make_pair(oooe, oool);
        }

    private:

        size_t indexof(wsrep_seqno_t seqno)
        {
            return (seqno & appliers_mask_);
        }

        bool may_enter(const TrxHandle* trx) const
        {
            return condition_(last_entered_, last_left_, trx);
        }

        // wait until it is possible to grab slot in monitor,
        // update last entered
        void pre_enter(TrxHandle* trx, gu::Lock& lock, const int idx)
        {
            assert(last_left_ <= last_entered_);

            while (last_entered_ >= last_left_ + appliers_size_)
            {
                if (trx != 0) { trx->unlock(); }

                lock.wait(pre_enter_cond_);

                if (trx != 0) { trx->lock(); }
            }

            if (trx != 0)
            {
                assert(appliers_[idx].state_ == Applier::S_IDLE ||
                       appliers_[idx].state_ == Applier::S_CANCELED);

                while (appliers_[indexof(last_entered_ + 1)].state_
                       > Applier::S_IDLE)
                {
                    Applier& a(appliers_[indexof(last_entered_ + 1)]);

                    if (a.state_ == Applier::S_WAITING &&
                        may_enter(a.trx_) == true)
                    {
                        a.cond_.signal();
                    }
                    ++last_entered_;
                }

                const wsrep_seqno_t trx_seqno(trx->get_global_seqno());
                if (last_entered_ + 1 == trx_seqno)
                {
                    last_entered_ = trx_seqno;
                }

                while (appliers_[indexof(last_entered_ + 1)].state_
                       > Applier::S_IDLE)
                {
                    Applier& a(appliers_[indexof(last_entered_ + 1)]);

                    if (a.state_ == Applier::S_WAITING &&
                        may_enter(a.trx_) == true)
                    {
                        a.cond_.signal();
                    }
                    ++last_entered_;
                }
            }
            pre_enter_cond_.broadcast();
        }

        Monitor(const Monitor&);
        void operator=(const Monitor&);

        const C condition_;
        Mode mode_;
        gu::Mutex mutex_;
        gu::Cond pre_enter_cond_;
        wsrep_seqno_t last_entered_;
        wsrep_seqno_t last_left_;
        std::vector<Applier> appliers_;
        size_t entered_; // entered
        size_t oooe_; // out of order entered
        size_t oool_; // out of order left
    };
}

#endif // GALERA_APPLY_MONITOR_HPP
