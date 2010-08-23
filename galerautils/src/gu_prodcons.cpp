// Copyright (C) 2009 Codership Oy <info@codership.com>

#include "gu_prodcons.hpp"

#include <cassert>
#include <deque>

using namespace std;

class gu::prodcons::MessageQueue
{
public:

    MessageQueue() : que() { }
    bool empty() const { return que.empty(); }
    size_t size() const { return que.size(); }
    const Message& front() const { return que.front(); }

    void pop_front() { que.pop_front(); }
    
    void push_back(const Message& msg) { que.push_back(msg); }

private:

    std::deque<Message> que;
};

void gu::prodcons::Producer::send(const Message& msg, Message* ack)
{
    cons.queue_and_wait(msg, ack);
}

const gu::prodcons::Message* gu::prodcons::Consumer::get_next_msg()
{
    const Message* ret = 0;
    Lock lock(mutex);
    if (mque->empty() == false)
    {
        ret = &mque->front();
    }
    return ret;
}

void gu::prodcons::Consumer::queue_and_wait(const Message& msg, Message* ack)
{
    Lock lock(mutex);
    mque->push_back(msg);
    if (mque->size() == 1)
    {
        notify();
    }
    lock.wait(msg.get_producer().get_cond());
    assert(&rque->front().get_producer() == &msg.get_producer());
    if (ack)
    {
        *ack = rque->front();
    }
    rque->pop_front();
    if (rque->empty() == false)
    {
        rque->front().get_producer().get_cond().signal();
    }    
}

void gu::prodcons::Consumer::return_ack(const Message& ack)
{
    Lock lock(mutex);
    assert(&ack.get_producer() == &mque->front().get_producer());
    rque->push_back(ack);
    mque->pop_front();
    if (rque->size() == 1)
    {
        ack.get_producer().get_cond().signal();
    } 
}

gu::prodcons::Consumer::Consumer() :
    mutex(),
    mque(new MessageQueue),
    rque(new MessageQueue)
{

}

gu::prodcons::Consumer::~Consumer()
{
    delete mque;
    delete rque;
}

