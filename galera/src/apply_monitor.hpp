
#ifndef GALERA_APPLY_MONITOR_HPP
#define GALERA_APPLY_MONITOR_HPP

#include "trx_handle.hpp"
#include "gu_lock.hpp"

#include <vector>

namespace galera
{
    class ApplyMonitor
    {
    private:
        struct Applier
        {
            Applier() : trx_(0), cond_(), state_(S_IDLE) { }
            const TrxHandle* trx_;
            gu::Cond cond_;
            enum State
            {
                S_IDLE,     // Slot is free
                S_WAITING,  // Waiting to enter applying critical section
                S_APPLYING, // Applying
                S_FINISHED  // Finished
            } state_;
            Applier(const Applier& other) 
                : 
                trx_(other.trx_), 
                cond_(other.cond_),
                state_(other.state_) { }
        private:
            void operator=(const Applier&);
        };
        static const size_t appliers_size_ = (1 << 14);
        static const size_t appliers_mask_ = appliers_size_ - 1;
    public:
        ApplyMonitor() 
            : 
            mutex_(), 
            last_entered_(-1), 
            last_left_(-1),
            appliers_(appliers_size_)
        { }

        ~ApplyMonitor()
        {
        }
        
        void assign_initial_position(wsrep_seqno_t seqno) 
        {
            assert(last_entered_ == last_left_);
            log_info << "initial position " << seqno;
            last_entered_ = last_left_ = seqno; 
        }
        
        size_t indexof(wsrep_seqno_t seqno)
        {
            return (seqno & appliers_mask_);
        }

        void update_last_entered(wsrep_seqno_t seqno)
        {
            if (last_entered_ + 1 == seqno)
            {
                last_entered_ = seqno;
            }
            while (appliers_[indexof(last_entered_ + 1)].state_ >= Applier::S_WAITING)
            {
                ++last_entered_;
            }
        }

        void enter_wait(const TrxHandle* trx)
        {
            while (enter(trx) == -EAGAIN)
            {
                static const struct timespec period = {0, 1000000};
                nanosleep(&period, 0);
            }
        }
        
        int enter(const TrxHandle* trx)
        {
            size_t idx(indexof(trx->get_global_seqno()));
            gu::Lock lock(mutex_);
            
            
            if (appliers_[idx].state_ != Applier::S_IDLE)
            {
                return -EAGAIN;
            }
            appliers_[idx].state_ = Applier::S_WAITING;
            appliers_[idx].trx_   = trx;
            // We must wait until all preceding trxs have entered
            // the monitor and the last dependent has left
            while (last_entered_ + 1 < trx->get_global_seqno() ||
                   last_left_        < trx->get_last_depends_seqno())
            {
                lock.wait(appliers_[idx].cond_);
            }
            update_last_entered(trx->get_global_seqno());
            appliers_[idx].state_ = Applier::S_APPLYING;            
            
            return 0;
        }
        
        void leave(const TrxHandle* trx)
        {
            size_t idx(indexof(trx->get_global_seqno()));
            gu::Lock lock(mutex_);
            
            assert(appliers_[idx].state_ == Applier::S_APPLYING);
            appliers_[idx].state_ = Applier::S_FINISHED;

            assert(appliers_[indexof(last_left_)].state_ == Applier::S_IDLE);
            
            for (wsrep_seqno_t i = last_left_ + 1; i <= last_entered_; ++i)
            {
                Applier& a(appliers_[indexof(i)]);
                switch (a.state_)
                {
                case Applier::S_FINISHED:
                    if (last_left_ + 1 == i)
                    {
                        a.state_ = Applier::S_IDLE;
                        last_left_ = i;
                    }
                    break;
                case Applier::S_WAITING:
                    assert(a.trx_ != 0);
                    if (last_entered_ + 1 >= a.trx_->get_global_seqno() &&
                        last_left_        >= a.trx_->get_last_depends_seqno())
                    {
                        a.cond_.signal();
                    }
                    break;
                case Applier::S_APPLYING:
                    break;
                default:
                    gu_throw_fatal << "invalid state " << a.state_;
                }
            }
            
            assert((last_left_ >= trx->get_global_seqno() && 
                    appliers_[idx].state_ == Applier::S_IDLE) ||
                   appliers_[idx].state_ == Applier::S_FINISHED);
            assert(last_left_ != last_entered_ ||
                   appliers_[indexof(last_left_)].state_ == Applier::S_IDLE);
            appliers_[idx].trx_ = 0;
        }
        
        void cancel(const TrxHandle* trx)
        {
            static const struct timespec period = {0, 1000000};
            while (enter(trx) == -EAGAIN)
            {
                nanosleep(&period, 0);
            }
            leave(trx);
        }
        
    private:
        
        ApplyMonitor(const ApplyMonitor&);
        void operator=(const ApplyMonitor&);
        
        gu::Mutex mutex_;
        wsrep_seqno_t last_entered_;
        wsrep_seqno_t last_left_;
        std::vector<Applier> appliers_;
    };
}




#endif // GALERA_APPLY_MONITOR_HPP
