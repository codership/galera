/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*! 
 * @file gu_poll.hpp Poll interface definitions
 */

#ifndef GU_POLL_HPP
#define GU_POLL_HPP

#include "gu_exception.hpp"

// Forward declarations
namespace gu
{
    namespace datetime
    {
        class Period;
    }
}


// Declarations
namespace gu
{
    namespace net
    {
        /*!
         * @brief Poll event enumeration.
         * 
         * Poll event enumeration. Note that only E_IN, E_OUT, E_ERROR 
         * and E_CLOSED are usually returned by poll implementation, 
         * other events are extensions that are used elsewhere.
         */
        enum PollEventEnum
        {
            E_IN        = 1 << 0, /*!< Socket is readable                */
            E_OUT       = 1 << 1, /*!< Socket is writable                */
            E_ACCEPTED  = 1 << 2, /*!< Socket was accepted               */
            E_CONNECTED = 1 << 3, /*!< Non-blocking socket was connected */
            E_ERROR     = 1 << 4, /*!< Socket operation failed           */
            E_CLOSED    = 1 << 5, /*!< Socket was closed by peer         */
            E_EMPTY     = 1 << 6  /*!< No events                         */
        };
        
        /*!
         * Event class encapsulating file descriptor, event mask and 
         * user defined pointer to arbitrary user data. Event mask 
         * is treated equivivalent of pollfd::events when passing
         * as parameter to Poll interface methods and pollfd::revents
         * when returned from Poll interface methods.
         */
        class PollEvent
        {
        public:
            /*!
             * Constructor.
             *
             * @param fd        File descriptor
             * @param events    Event mask
             * @param user_data Pointer to user data
             */
            PollEvent(int fd, int events, void* user_data) : 
                fd_       (fd       ),
                events_   (events   ),
                user_data_(user_data)
            { }
            
            /*!
             * Get associated file descriptor.
             *
             * @return File descriptor
             */
            int   get_fd()        const { return fd_       ; }
            
            /*!
             * Get associated event mask.
             *
             * @return Event mask
             */
            int   get_events()    const { return events_   ; }
            
            /*!
             * Get associated user data.
             *
             * @return Pointer to user data
             */
            void* get_user_data() const { return user_data_; }
            
        private:
            int   const fd_;
            int   const events_;
            void* const user_data_;
        };
        
        /*!
         * Poll interface.
         */
        class Poll
        {
        public:
            /*!
             * Default constructor.
             */
            Poll() { }
            
            /*!
             * Default destructor.
             */
            virtual ~Poll() { }

            /*!
             * Insert entry described by event into poll system.
             *
             * @param ev PollEvent
             */
            virtual void insert(const PollEvent& ev) = 0;

            /*!
             * Erase entry described by event from poll system.
             *
             * @param ev PollEvent
             */
            virtual void erase(const PollEvent& ev) = 0;

            /*!
             * Modify existing entry described by event.
             *
             * @param ev PollEvent
             */
            virtual void modify(const PollEvent& ev) = 0;

            /*!
             * Poll for events until there are events to return 
             * or time given in @p has passed. Negative value
             * of @p denotes infinite time period. Note that 
             * even with infinite timeout poll may return without
             * events if it is interrupted by signal. 
             *
             * @param p Period
             */
            virtual void poll(const gu::datetime::Period& p) = 0;

            /*!
             * Check if all events returned by previous poll call
             * have been handled.
             *
             * @return True if all events have been handled, false if
             *         there are unhandled events
             */
            virtual bool empty() const = 0;
            
            /*!
             * Get first unhandled event.
             *
             * @return First unhandled event
             *
             * @throw gu::Exception If there were no unhandled events available
             */
            virtual PollEvent front() const throw (gu::Exception) = 0;

            /*!
             * Pop event after it has been handled.
             *
             * @throw gu::Exception If there were no unhandled events
             */
            virtual void pop_front() throw (gu::Exception) = 0;

            /*!
             * Factory method for creating Poll object.
             *
             * @return Pointer to created Poll object
             */
            static Poll* create();
        };
    }
}

#endif // GU_POLL_HPP
