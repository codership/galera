//
// Copyright (C) 2010 Codership Oy
//

#ifndef GALERA_MONITOR_HPP
#define GALERA_MONITOR_HPP

#include "trx_handle.hpp"
#include <galerautils.hpp>

#include <vector>

namespace galera
{
    template <class C>
    class Monitor
    {
    private:

        struct Process
        {
            Process() : obj_(0), cond_(), state_(S_IDLE) { }

            Process(const Process& other)
                :
                obj_  (other.obj_),
                cond_ (other.cond_),
                state_(other.state_)
            { }

            const C* obj_;
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

            void operator=(const Process&);
        };

        static const size_t process_size_ = (1 << 14);
        static const size_t process_mask_ = process_size_ - 1;

    public:

        Monitor()
            :
            mutex_(),
            cond_(),
            last_entered_(-1),
            last_left_(-1),
            drain_seqno_(-1),
            process_(process_size_),
            entered_(0),
            oooe_(0),
            oool_(0),
            win_size_(0)
        { }

        ~Monitor()
        {
            if (entered_ > 0)
            {
                log_info << "mon: entered " << entered_
                         << " oooe fraction " << double(oooe_)/entered_
                         << " oool fraction " << double(oool_)/entered_;
            }
            else
            {
                log_info << "apply mon: entered 0";
            }
        }

        void set_initial_position(wsrep_seqno_t seqno)
        {
            gu::Lock lock(mutex_);
            if (last_entered_ == -1 || seqno == -1)
            {
                // first call or reset
                last_entered_ = last_left_ = seqno;
            }
            else
            {
                // drain monitor up to seqno but don't reset last_entered_
                // or last_left_
                drain_common(seqno, lock);
            }
        }

        int enter(C& obj)
        {
            const wsrep_seqno_t obj_seqno(obj.seqno());
            const size_t        idx(indexof(obj_seqno));
            gu::Lock            lock(mutex_);

            pre_enter(obj, lock);

            if (process_[idx].state_ !=  Process::S_CANCELED)
            {
                assert(process_[idx].state_ == Process::S_IDLE);

                process_[idx].state_ = Process::S_WAITING;
                process_[idx].obj_   = &obj;

                while (may_enter(obj) == false &&
                       process_[idx].state_ == Process::S_WAITING)
                {
                    obj.unlock();
                    lock.wait(process_[idx].cond_);
                    obj.lock();
                }

                if (process_[idx].state_ != Process::S_CANCELED)
                {
                    assert(process_[idx].state_ == Process::S_WAITING ||
                           process_[idx].state_ == Process::S_APPLYING);

                    process_[idx].state_ = Process::S_APPLYING;

                    ++entered_;
                    oooe_     += (last_left_ + 1 < obj_seqno);
                    win_size_ += (last_entered_ - last_left_);
                    return 0;
                }
            }

            assert(process_[idx].state_ == Process::S_CANCELED);
            process_[idx].state_ = Process::S_IDLE;

            return -EINTR;
        }

        void leave(const C& obj)
        {
#ifndef NDEBUG
            wsrep_seqno_t obj_seqno(obj.seqno());
            size_t   idx(indexof(obj_seqno));
#endif // NDEBUG
            gu::Lock lock(mutex_);

            assert(process_[idx].state_ == Process::S_APPLYING ||
                   process_[idx].state_ == Process::S_CANCELED);

            assert(process_[indexof(last_left_)].state_ == Process::S_IDLE);

            post_leave(obj, lock);
        }

        void self_cancel(C& obj)
        {

#ifndef NDEBUG
            size_t   idx(indexof(obj.seqno()));
#endif // NDEBUG
            gu::Lock lock(mutex_);

            pre_enter(obj, lock);

            assert(process_[idx].state_ == Process::S_IDLE ||
                   process_[idx].state_ == Process::S_CANCELED);

            post_leave(obj, lock);
        }

        void interrupt(const C& obj)
        {

            size_t   idx (indexof(obj.seqno()));
            gu::Lock lock(mutex_);

            while (obj.seqno() - last_left_ >=
                   static_cast<ssize_t>(process_size_)) // TODO: exit on error
            {
                lock.wait(cond_);
            }

            if (process_[idx].state_ == Process::S_WAITING)
            {
                process_[idx].state_ = Process::S_CANCELED;
                process_[idx].cond_.signal();
                // since last_left + 1 cannot be <= S_WAITING we're not
                // modifying a window here. No broadcasting.
            }
            else
            {
                log_debug << "cancel, applier state " << process_[idx].state_;
            }
        }

        wsrep_seqno_t last_left() const { return last_left_; }

        void drain(wsrep_seqno_t seqno)
        {
            gu::Lock lock(mutex_);
            drain_common(seqno, lock);
        }

        void get_stats(double* oooe, double* oool, double* win_size)
        {
            gu::Lock lock(mutex_);

            if (entered_ > 0)
            {
                *oooe = (oooe_ > 0 ? double(oooe_)/entered_ : .0);
                *oool = (oool_ > 0 ? double(oool_)/entered_ : .0);
                *win_size = (win_size_ > 0 ? double(win_size_)/entered_ : .0);
            }
            else
            {
                *oooe = .0; *oool = .0; *win_size = .0;
            }

            oooe_ = 0; oool_ = 0; win_size_ = 0; entered_ = 0;
        }

    private:

        size_t indexof(wsrep_seqno_t seqno)
        {
            return (seqno & process_mask_);
        }

        bool may_enter(const C& obj) const
        {
            return obj.condition(last_entered_, last_left_);
        }

        // wait until it is possible to grab slot in monitor,
        // update last entered
        void pre_enter(C& obj, gu::Lock& lock)
        {
            assert(last_left_ <= last_entered_);

            const wsrep_seqno_t obj_seqno(obj.seqno());

            while (obj_seqno - last_left_ >=
                   static_cast<ssize_t>(process_size_)) // TODO: exit on error
            {
                obj.unlock();
                lock.wait(cond_);
                obj.lock();
            }

            if (last_entered_ < obj_seqno) last_entered_ = obj_seqno;
        }

        void post_leave(const C& obj, gu::Lock& lock)
        {
            const wsrep_seqno_t obj_seqno(obj.seqno());
            const size_t idx(indexof(obj_seqno));

            if (last_left_ + 1 == obj_seqno) // we're shrinking window
            {
                process_[idx].state_ = Process::S_IDLE;
                last_left_           = obj_seqno;

                for (wsrep_seqno_t i = last_left_ + 1; i <= last_entered_; ++i)
                {
                    Process& a(process_[indexof(i)]);

                    if (Process::S_FINISHED == a.state_)
                    {
                        a.state_   = Process::S_IDLE;
                        last_left_ = i;
                    }
                    else
                    {
                        break;
                    }
                }
                // wake up waiters that may remain above us (last_left_
                // now is max)
                for (wsrep_seqno_t i = last_left_ + 1; i <= last_entered_; ++i)
                {
                    Process& a(process_[indexof(i)]);
                    if (a.state_           == Process::S_WAITING &&
                        may_enter(*a.obj_) == true)
                    {
                        // We need to set state to APPLYING here because if
                        // it is  the last_left_ + 1 and it gets canceled in
                        // the race  that follows exit from this function,
                        // there will be  nobody to clean up and advance
                        // last_left_.
                        a.state_ = Process::S_APPLYING;
                        a.cond_.signal();
                    }
                }
            }
            else
            {
                process_[idx].state_ = Process::S_FINISHED;
            }

            process_[idx].obj_ = 0;
            assert((last_left_ >= obj_seqno &&
                    process_[idx].state_ == Process::S_IDLE) ||
                   process_[idx].state_ == Process::S_FINISHED);
            assert(last_left_ != last_entered_ ||
                   process_[indexof(last_left_)].state_ == Process::S_IDLE);

            if ((last_left_ >= obj_seqno) || // occupied window shrinked
                (last_left_ == drain_seqno_)) // draining requested
            {
                oool_ += (last_left_ > obj_seqno);
                cond_.broadcast();
            }
        }

        void drain_common(wsrep_seqno_t seqno, gu::Lock& lock)
        {
            log_debug << "draining up to " << seqno;
            drain_seqno_ = seqno;

            while (drain_seqno_ - last_left_ >=
                   static_cast<ssize_t>(process_size_)) // TODO: exit on error
            {
                lock.wait(cond_);
            }

            if (last_left_ > drain_seqno_)
            {
                log_debug << "last left greater than drain seqno";
                for (wsrep_seqno_t i = drain_seqno_; i <= last_left_; ++i)
                {
                    const Process& a(process_[indexof(i)]);
                    log_debug << "applier " << i
                              << " in state " << a.state_;
                }
            }

            while (last_left_ < drain_seqno_)
            {
                lock.wait(cond_);
            }
            drain_seqno_ = -1;
        }

        Monitor(const Monitor&);
        void operator=(const Monitor&);

        gu::Mutex mutex_;
        gu::Cond  cond_;
        wsrep_seqno_t last_entered_;
        wsrep_seqno_t last_left_;
        wsrep_seqno_t drain_seqno_;
        std::vector<Process> process_;
        long entered_;  // entered
        long oooe_;     // out of order entered
        long oool_;     // out of order left
        long win_size_; // window between last_left_ and last_entered_
    };
}

#endif // GALERA_APPLY_MONITOR_HPP
