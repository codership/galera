//
// Copyright (C) 2019 Codership Oy <info@codership.com>
//

/**
 * Segmentation aware send queue implementation.
 *
 * In order to avoid the segment relay node of hogging all bandwidth
 * for bulk transfers, the send queue needs to be aware of segments.
 * FairSendQueue implements a queue which maintains separate queue
 * for each segment. Messages are read from queues in round robin.
 */

#ifndef GCOMM_FAIR_SEND_QUEUE_HPP
#define GCOMM_FAIR_SEND_QUEUE_HPP

#include "gcomm/datagram.hpp"

#include <deque>
#include <map>

namespace gcomm
{
    class FairSendQueue
    {
        typedef std::map<int, std::deque<gcomm::Datagram> > queue_type;
    public:
        FairSendQueue()
            : current_segment_(-1)
            , last_pushed_segment_(-1)
            , queued_bytes_()
            , queue_()
        { }

        /* Push back datagram dg from segment. */
        void push_back(int segment, const gcomm::Datagram& dg)
        {
            assert(current_segment_ != -1 || empty());
            assert(queued_bytes_ || empty());
            std::deque<gcomm::Datagram>& dq(queue_[segment]);
            dq.push_back(dg);
            if (current_segment_ == -1)
            {
                current_segment_ = segment;
            }
            last_pushed_segment_ = segment;
            queued_bytes_ += dg.len();
        }

        /* Return reference to front datagram. */
        const gcomm::Datagram& front() const
        {
            assert(current_segment_ != -1);
            queue_type::const_iterator i(queue_.find(current_segment_));
            assert(i != queue_.end());
            return i->second.front();
        }

        /* Return reference to back datagram. */
        const gcomm::Datagram& back() const
        {
            assert(last_pushed_segment_ != -1);
            queue_type::const_iterator i(queue_.find(last_pushed_segment_));
            assert(i != queue_.end());
            return i->second.back();
        }

        /* Pop front element from the queue. */
        void pop_front()
        {
            assert(current_segment_ != -1);
            assert(not queue_[current_segment_].empty());
            std::deque<gcomm::Datagram>& que(queue_[current_segment_]);
            assert(que.front().len() <= queued_bytes_);
            queued_bytes_ -= que.front().len();
            que.pop_front();
            current_segment_ = get_next_segment();
        }

        /* Return true if queue is empty. */
        bool empty() const
        {
            return (queued_bytes() == 0);
        }

        /* Return queue size. */
        size_t size() const
        {
            size_t ret(0);
            for (queue_type::const_iterator i(queue_.begin());
                 i != queue_.end(); ++i)
            {
                ret += i->second.size();
            }
            return ret;
        }

        size_t queued_bytes() const
        {
            return queued_bytes_;
        }

        /* Return number of queued messages for each segment. */
        std::vector<std::pair<int, size_t> > segments() const
        {
            std::vector<std::pair<int, size_t> > ret;
            for (queue_type::const_iterator i(queue_.begin());
                 i != queue_.end(); ++i)
            {
                ret.push_back(std::make_pair(i->first, i->second.size()));
            }
            return ret;
        }
    private:
        int get_next_segment() const
        {
            queue_type::const_iterator i(queue_.find(current_segment_));
            assert(i != queue_.end());
            do
            {
                // Increment and wrap around
                ++i;
                if (i == queue_.end()) i = queue_.begin();

                if (not i->second.empty()) return i->first;
            }
            while (i->first != current_segment_);

            return -1;
        }

        int current_segment_;
        int last_pushed_segment_;
        size_t queued_bytes_;
        queue_type queue_;
    };
}

#endif /* GCOMM_FAIR_SEND_QUEUE */
