/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file gu_epoll.hpp Poll interface implementation using epoll
 * 
 */

#ifndef __GU_EPOLL_HPP__
#define __GU_EPOLL_HPP__

#include <vector>
#include "gu_poll.hpp"

// Declarations
namespace gu
{
    namespace net
    {
        class EPoll;
    }
}

struct epoll_event;

#if 0
/*!
 * @brief Poll event class
 *
 */
class gu::net::EPollEvent
{
    int fd;          /*! File descriptor */
    int events;      /*! Event mask */
    void* user_data; /*! Pointer to user context */
public:
    /*!
     * @brief EPollEvent Constructor
     *
     * @param fd_ File descriptor
     * @param events_ Event mask
     * @param user_data_ User data
     */
    EPollEvent(int fd_, int events_, void* user_data_) :
        fd(fd_),
        events(events_),
        user_data(user_data_)
    {
    }
    
    /*!
     * @brief Get file descriptor corresponding to event
     */
    int get_fd() const
    {
        return fd;
    }
    
    /*!
     * @brief Get event mask corresponding to event
     */
    int get_events() const
    {
        return events;
    }
    
    /*!
     * @brief Get user data corresponding to event
     */
    void* get_user_data() const
    {
        return user_data;
    }
};

#endif

/*!
 * @brief EPoll interface
 */
class gu::net::EPoll : public Poll
{
    int e_fd; /*! epoll control file descriptor */
    int n_events;
    std::vector<epoll_event> events; /*! array of epoll events */
    std::vector<epoll_event>::iterator current; /*! pointer to current event */
    
    EPoll(const EPoll&);
    void operator=(const EPoll&);
public:
    
    /*!
     * @brief Constructor
     */
    EPoll();
    
    /*!
     * @brief Destructor
     */
    ~EPoll();
    
    /*!
     * @brief Insert new file descriptor to poll for
     *
     * EPollEvent @p ev must contain valid file descriptor and 
     * initial event mask. 
     * 
     * @param ev EPollEvent containing required data
     *
     * @throws std::runtime_error if epoll control call fails
     * @throws std::bad_alloc if call fails to allocate memory for 
     *         internal storage
     */
    void insert(const PollEvent& ev);
    
    /*!
     * @brief Erase file descriptor from set of polled fds
     * 
     * EPollEvent @p ev must contain valid file descriptor
     *
     * @param ev EPollEvent containing valid file descriptor
     */
    void erase(const PollEvent& ev);
    
    /*!
     * @brief Modify event mask for polled events of existing file descriptor
     *
     * @param ev EPollEvent containing valid file descriptor
     *
     * @throws std::runtime_error if epoll control call fails
     */
    void modify(const PollEvent& ev);
    
    /*!
     * @brief Poll until activity is detected or timeout expires
     *
     * Poll file descriptor until activity is detected or timeout 
     * expires. Timeout is given in milliseconds, -1 means poll 
     * indefinitely and 0 means return immediately. After 
     * this call returns, list of active events is stored in
     * internal storage.
     *
     * @param timeout Timeout in milliseconds
     */
    void poll(const gu::datetime::Period& p);
    
    /*!
     * @brief Check whether list of unhandled poll events is empty
     */
    bool empty() const;
    
    /*!
     * @brief Get (first) event from list of unhandled poll events
     *
     * @return EPollEvent 
     *
     * @throws std::logic_error if no events were available
     */
    PollEvent front() const;
    
    /*!
     * @brief Erase the first event from the list of unhandled poll events
     *
     * @throws std::logic_error if no events were available
     */
    void pop_front();
};

#endif /* __GU_EPOLL_HPP__ */

