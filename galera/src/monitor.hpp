//
// Copyright (C) 2010-2013 Codership Oy
//

#ifndef GALERA_MONITOR_HPP
#define GALERA_MONITOR_HPP

#include "trx_handle.hpp"
#include <gu_lock.hpp> // for gu::Mutex and gu::Cond
#include <gu_limits.h>

#include <vector>

namespace galera
{
    template <class C>
    class Monitor
    {
    private:

        struct Process
        {
            Process() : obj_(0), cond_(), wait_cond_(), state_(S_IDLE)
#ifndef NDEBUG
                      ,dobj_()
#endif /* NDEBUG */
            { }

            const C* obj_;
            gu::Cond cond_;
            gu::Cond wait_cond_;
            enum State
            {
                S_IDLE,     // Slot is free
                S_WAITING,  // Waiting to enter applying critical section
                S_CANCELED,
                S_APPLYING, // Applying
                S_FINISHED  // Finished
            } state_;
#ifndef NDEBUG
            C dobj_;
#endif /* NDEBUG */

        private:

            // non-copyable
            Process(const Process& other);
            void operator=(const Process&);
        };

        static const ssize_t process_size_ = (1ULL << 16);
        static const size_t  process_mask_ = process_size_ - 1;

    public:

        Monitor()
            :
            mutex_(),
            cond_(),
            uuid_(WSREP_UUID_UNDEFINED),
            last_entered_(-1),
            last_left_(-1),
            drain_seqno_(GU_LLONG_MAX),
            process_(new Process[process_size_]),
            entered_(0),
            oooe_(0),
            oool_(0),
            win_size_(0)
        { }

        ~Monitor()
        {
            delete[] process_;
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

        /*
         * For ordered CC events this had to be changed:
         * - it either resets position to -1 or
         * - merely advances it to seqno if current position is behind.
         * Assumes that monitor has been drained.
         */
        void set_initial_position(const wsrep_uuid_t& uuid,
                                  wsrep_seqno_t const seqno)
        {
            gu::Lock lock(mutex_);

            state_debug_print("set_initial_position", seqno);

            uuid_ = uuid;
            if (last_entered_ == -1 || seqno == -1)
            {
                // first call or reset
                last_entered_ = last_left_ = seqno;
            }
            else
#if 1 // now
            {
                if (last_left_    < seqno)      last_left_    = seqno;
                if (last_entered_ < last_left_) last_entered_ = last_left_;
            }

            // some drainers may wait for us here
            cond_.broadcast();
#else // before
            {
                // drain monitor up to seqno but don't reset last_entered_
                // or last_left_
                drain_common(seqno, lock);
                drain_seqno_ = GU_LLONG_MAX;
            }
#endif
            if (seqno != -1)
            {
                const size_t idx(indexof(seqno));
                process_[idx].wait_cond_.broadcast();
            }
        }

        void enter(C& obj)
        {
            const wsrep_seqno_t obj_seqno(obj.seqno());
            const size_t        idx(indexof(obj_seqno));
            gu::Lock            lock(mutex_);

            state_debug_print("enter", obj);
            assert(obj_seqno > last_left_);

            pre_enter(obj, lock);

            if (gu_likely(process_[idx].state_ != Process::S_CANCELED))
            {
                assert(process_[idx].state_ == Process::S_IDLE);

                process_[idx].state_ = Process::S_WAITING;
                process_[idx].obj_   = &obj;
#ifndef NDEBUG
                process_[idx].dobj_.~C();
                new (&process_[idx].dobj_) C(obj);
#endif /* NDEBUG */
#ifdef GU_DBUG_ON
                obj.debug_sync(mutex_);
#endif // GU_DBUG_ON
                while (may_enter(obj) == false &&
                       process_[idx].state_ == Process::S_WAITING)
                {
                    lock.wait(process_[idx].cond_);
                }

                if (process_[idx].state_ != Process::S_CANCELED)
                {
                    assert(process_[idx].state_ == Process::S_WAITING ||
                           process_[idx].state_ == Process::S_APPLYING);

                    process_[idx].state_ = Process::S_APPLYING;

                    ++entered_;
                    oooe_     += ((last_left_ + 1) < obj_seqno);
                    win_size_ += (last_entered_ - last_left_);
                    return;
                }
            }

            assert(process_[idx].state_ == Process::S_CANCELED);
            process_[idx].state_ = Process::S_IDLE;

            state_debug_print("enter canceled", obj);
            gu_throw_error(EINTR);
        }

        bool entered(const C& obj) const
        {
            return state(obj) == Process::S_APPLYING;
        }

        bool finished(const C& obj) const
        {
            return state(obj) == Process::S_FINISHED;
        }

        bool canceled(const C& obj) const
        {
            return state(obj) == Process::S_CANCELED;
        }

        void leave(const C& obj)
        {
#ifndef NDEBUG
            size_t   idx(indexof(obj.seqno()));
#endif /* NDEBUG */
            gu::Lock lock(mutex_);
            state_debug_print("leave", obj);

            assert(process_[idx].state_ == Process::S_APPLYING ||
                   process_[idx].state_ == Process::S_CANCELED);

            assert(process_[indexof(last_left_)].state_ == Process::S_IDLE);

            post_leave(obj.seqno(), lock);
        }

        void self_cancel(C& obj)
        {
            wsrep_seqno_t const obj_seqno(obj.seqno());
            size_t   idx(indexof(obj_seqno));
            gu::Lock lock(mutex_);

            state_debug_print("self_cancel", obj);

            assert(obj_seqno > last_left_);

            while (obj_seqno - last_left_ >= process_size_)
                // TODO: exit on error
            {
                log_warn << "Trying to self-cancel seqno out of process "
                         << "space: obj_seqno - last_left_ = " << obj_seqno
                         << " - " << last_left_ << " = "
                         << (obj_seqno - last_left_)
                         << ", process_size_: "  << process_size_
                         << ". Deadlock is very likely.";

                lock.wait(cond_);
            }

            assert(process_[idx].state_ == Process::S_IDLE ||
                   process_[idx].state_ == Process::S_CANCELED);

#ifndef NDEBUG
            process_[idx].dobj_.~C();
            new (&process_[idx].dobj_) C(obj);
#endif /* NDEBUG */

            if (obj_seqno > last_entered_) last_entered_ = obj_seqno;

            if (obj_seqno <= drain_seqno_)
            {
                post_leave(obj.seqno(), lock);
            }
            else
            {
                process_[idx].state_ = Process::S_FINISHED;
            }
        }

        bool interrupt(const C& obj)
        {
            size_t   idx (indexof(obj.seqno()));
            gu::Lock lock(mutex_);

            while (obj.seqno() - last_left_ >= process_size_)
                // TODO: exit on error
            {
                lock.wait(cond_);
            }

            state_debug_print("interrupt", obj);

            if ((process_[idx].state_ == Process::S_IDLE &&
                 obj.seqno()          >  last_left_ ) ||
                process_[idx].state_ == Process::S_WAITING )
            {
                process_[idx].state_ = Process::S_CANCELED;
                process_[idx].cond_.signal();
                // since last_left + 1 cannot be <= S_WAITING we're not
                // modifying a window here. No broadcasting.
                return true;
            }
            else
            {
                log_debug << "interrupting " << obj.seqno()
                          << " state " << process_[idx].state_
                          << " le " << last_entered_
                          << " ll " << last_left_;
            }

            return false;
        }

        wsrep_seqno_t last_left() const
        {
            gu::Lock lock(mutex_);
            return last_left_;
        }

        wsrep_seqno_t last_entered() const
        {
            gu::Lock lock(mutex_);
            return last_entered_;
        }

        void last_left_gtid(wsrep_gtid_t& gtid) const
        {
            gu::Lock lock(mutex_);
            gtid.uuid = uuid_;
            gtid.seqno = last_left_;
        }

        ssize_t       size()        const { return process_size_; }

        bool would_block (wsrep_seqno_t seqno) const
        {
            return (seqno - last_left_ >= process_size_ ||
                    seqno > drain_seqno_);
        }

        void drain(wsrep_seqno_t seqno)
        {
            gu::Lock lock(mutex_);

            state_debug_print("drain", seqno);

            while (drain_seqno_ != GU_LLONG_MAX)
            {
                lock.wait(cond_);
            }

            drain_common(seqno, lock);

            // there can be some stale canceled entries
            update_last_left();

            drain_seqno_ = GU_LLONG_MAX;
            cond_.broadcast();
        }

        void wait(wsrep_seqno_t seqno)
        {
            gu::Lock lock(mutex_);
            while (last_left_ < seqno)
            {
                size_t idx(indexof(seqno));
                lock.wait(process_[idx].wait_cond_);
            }
        }

        void wait(gu::GTID& gtid, const gu::datetime::Date& wait_until)
        {
            gu::Lock lock(mutex_);
            if (gtid.uuid() != uuid_)
            {
                throw gu::NotFound();
            }
            while (last_left_ < gtid.seqno())
            {
                size_t idx(indexof(gtid.seqno()));
                lock.wait(process_[idx].wait_cond_, wait_until);
            }
        }

        void get_stats(double* oooe, double* oool, double* win_size) const
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
        }

        void flush_stats()
        {
            gu::Lock lock(mutex_);
            oooe_ = 0; oool_ = 0; win_size_ = 0; entered_ = 0;
        }

    private:

        template <typename T>
        void state_debug_print(const std::string& method, const T& x)
        {
// #define GALERA_MONITOR_DEBUG_PRINT
#ifdef GALERA_MONITOR_DEBUG_PRINT
            log_info << typeid(C).name() << ": " << method << "(" << x
                     << "): le " << last_entered_ << ", ll " << last_left_;
#endif /* GALERA_MONITOR_DEBUG_PRINT */
        }

        size_t indexof(wsrep_seqno_t seqno) const
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

            while (would_block (obj_seqno)) // TODO: exit on error
            {
                lock.wait(cond_);
            }

            if (last_entered_ < obj_seqno) last_entered_ = obj_seqno;
        }

        void update_last_left()
        {
            for (wsrep_seqno_t i = last_left_ + 1; i <= last_entered_; ++i)
            {
                Process& a(process_[indexof(i)]);

                if (Process::S_FINISHED == a.state_)
                {
                    a.state_   = Process::S_IDLE;
                    last_left_ = i;
                    a.wait_cond_.broadcast();
                }
                else
                {
                    break;
                }
            }
            assert(last_left_ <= last_entered_);
        }

        void wake_up_next()
        {
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

        void post_leave(wsrep_seqno_t const obj_seqno, gu::Lock& lock)
        {
            const size_t idx(indexof(obj_seqno));

            if (last_left_ + 1 == obj_seqno) // we're shrinking window
            {
                process_[idx].state_ = Process::S_IDLE;
                last_left_           = obj_seqno;
                process_[idx].wait_cond_.broadcast();

                update_last_left();
                oool_ += (last_left_ > obj_seqno);
                // wake up waiters that may remain above us (last_left_
                // now is max)
                wake_up_next();
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

            if ((last_left_ >= obj_seqno) ||  // - occupied window shrinked
                (last_left_ >= drain_seqno_)) // - this is to notify drain that
                                              //   we reached drain_seqno_
            {
                cond_.broadcast();
            }
        }

        void drain_common(wsrep_seqno_t seqno, gu::Lock& lock)
        {
            log_debug << "draining up to " << seqno;

            drain_seqno_ = seqno;

            if (last_left_ > drain_seqno_)
            {
                log_warn << "last left " << last_left_
                         << " greater than drain seqno " << drain_seqno_;
#ifndef NDEBUG
                for (wsrep_seqno_t i = drain_seqno_; i <= last_left_; ++i)
                {
                    const Process& a(process_[indexof(i)]);
                    log_info << "applier " << i
                             << " in state " << a.state_;
                }
#endif
            }

            while (last_left_ < drain_seqno_) lock.wait(cond_);
        }

        typename Process::State state(const C& obj) const
        {
            const wsrep_seqno_t obj_seqno(obj.seqno());
            const size_t        idx(indexof(obj_seqno));
            gu::Lock lock(mutex_);
            while (would_block (obj_seqno))
            {
                lock.wait(cond_);
            }
            return process_[idx].state_;
        }

        Monitor(const Monitor&);
        void operator=(const Monitor&);

        mutable
        gu::Mutex mutex_;
        gu::Cond  cond_;
        wsrep_uuid_t  uuid_;
        wsrep_seqno_t last_entered_;
        wsrep_seqno_t last_left_;
        wsrep_seqno_t drain_seqno_;
        Process*      process_;
        long entered_;  // entered
        long oooe_;     // out of order entered
        long oool_;     // out of order left
        long win_size_; // window between last_left_ and last_entered_
    };
}

#endif // GALERA_APPLY_MONITOR_HPP
