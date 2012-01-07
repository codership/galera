/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id:$
 */

/*!
 * @file gu_prodcons.hpp Synchronous producer/consumer interface
 */

#include "gu_lock.hpp"
// For byte_t
#include "gu_buffer.hpp"

/* Forward declarations */
namespace gu
{
    namespace prodcons
    {
        class MessageData;
        class Message;
        class MessageQueue;
        class Producer;
        class Consumer;
    }
}

class gu::prodcons::MessageData
{
public:
    virtual ~MessageData() { }
};

/*!
 * @brief Message class for Producer/Consumer communication
 */
class gu::prodcons::Message
{
    Producer* producer; /*! Producer associated to this message */
    int val; /*! Integer value (command/errno) */
    const MessageData* data;


public:
    /*!
     * @brief Constructor
     *
     * @param prod_ Producer associated to the message
     * @param data_ Message data
     * @param val_ Integer value associated to the message
     */
    Message(Producer*          prod_ = 0, 
            const MessageData* data_ = 0,
            int                val_  = -1) :
        producer(prod_),
        val(val_),
        data(data_)
    { }

    Message(const Message& msg) :
        producer(msg.producer),
        val(msg.val),
        data(msg.data)
    { }

    Message& operator=(const Message& msg)
    {
        producer = msg.producer;
        val = msg.val;
        data = msg.data;
        return *this;
    }

    /*!
     * @brief Get producer associated to the message
     *
     * @return Producer associated to the message
     */
    Producer& get_producer() const { return *producer; }

    /*!
     * @brief Get data associated to the message
     *
     * @return Data associated to the message
     */
    const MessageData* get_data() const { return data; }

    /*!
     * @brief Get int value associated to the message
     *
     * @return Int value associated to the message
     */
    int get_val() const { return val; }
};

/*!
 * @brief Producer interface
 */
class gu::prodcons::Producer
{
    gu::Cond cond;  /*! Condition variable */
    Consumer& cons; /*! Consumer associated to this producer */

    /*!
     * @brief Return reference to condition variable
     *
     * @return Reference to condition variable
     */
    Cond& get_cond() { return cond; }
    friend class Consumer;
public:
    /*!
     * @brief Consturctor
     *
     * @param cons_ Consumer associated to this producer
     */
    Producer(Consumer& cons_) :
        cond(),
        cons(cons_)
    { }

    /*!
     * @brief Send message to the consumer and wait for response
     *
     * @param[in] msg Message to be sent to consumer
     * @param[out] ack Ack message returned by the Consumer, containing error code
     */
    void send(const Message& msg, Message* ack);
};

/*!
 * @brief Consumer interface
 */
class gu::prodcons::Consumer
{
    Mutex mutex; /*! Mutex for internal locking */
    MessageQueue* mque; /*! Message queue for producer messages */
    MessageQueue* rque; /*! Message queue for ack messages */

    Consumer(const Consumer&);
    void operator=(const Consumer&);
protected:
    /*!
     * @brief Get the first message from the message queue
     *
     * Get the first message from the message queue. Note that 
     * this method does not remove the first message from message queue.
     * 
     * @return Next message from the message queue
     */
    const Message* get_next_msg();

    /*!
     * @brief Return ack message for the producer 
     *
     * Return ack message for the producer. Note that this method
     * pops the first message from the message queue.
     *
     * @param msg Ack message corresponding the current head of mque
     */
    void return_ack(const Message& msg);

    /*!
     * @brief Virtual method to notify consumer about queued message
     */
    virtual void notify() = 0;
public:
    /*!
     * @brief Default constructor
     */
    Consumer();

    /*!
     * @brief Default destructor
     */
    virtual ~Consumer();

    /*!
     * @brief Queue message and wait for ack
     *
     * @param[in] msg Message to be queued
     * @param[out] ack Ack returned by consumer
     */
    void queue_and_wait(const Message& msg, Message* ack);
};
