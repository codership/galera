/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef GU_POLL_HPP
#define GU_POLL_HPP

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
        enum
        {
            E_IN        = 1 << 0,
            E_OUT       = 1 << 1,
            E_ACCEPTED  = 1 << 2,
            E_CONNECTED = 1 << 3,
            E_ERROR     = 1 << 4,
            E_CLOSED    = 1 << 5,
            E_EMPTY     = 1 << 6
        };
        
        class PollEvent
        {
        public:
            PollEvent(int fd_, int events_, void* user_data_) : 
                fd(fd_),
                events(events_),
                user_data(user_data_)
            { }
            
            int get_fd() const { return fd; }
            int get_events() const { return events; }
            void* get_user_data() const { return user_data; }
            
        private:
            
            int fd;
            int events;
            void* user_data;
        };


        class Poll
        {
        public:
            Poll() { }
            virtual ~Poll() { }
            
            virtual void insert(const PollEvent& ev) = 0;
            virtual void erase(const PollEvent& ev) = 0;
            virtual void modify(const PollEvent& ev) = 0;
            virtual void poll(const gu::datetime::Period& p) = 0;
            
            virtual bool empty() const = 0;
            virtual PollEvent front() const = 0;
            virtual void pop_front() = 0;

            static Poll* create();
            
        };
    }
}

#endif // GU_POLL_HPP
