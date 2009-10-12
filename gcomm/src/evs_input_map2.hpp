/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file Input map for EVS messaging, take two
 */

#ifndef EVS_INPUT_MAP2_HPP
#define EVS_INPUT_MAP2_HPP

#include "evs_seqno.hpp"
#include "evs_message2.hpp"

#include "gcomm/map.hpp"


namespace gcomm
{
    class ReadBuf;
    namespace evs
    {
        class InputMap;
        class InputMapMsg;
        class InputMapMsgIndex;
        class InputMapMsgIterator;
        class InputMapNode;
        class InputMapNodeIndex;
    } // namespace evs
} // namespace gcomm


class gcomm::evs::InputMapMsg
{
public:
    InputMapMsg(const gcomm::UUID& uuid_, const Message& msg_, 
                gcomm::ReadBuf* const rb_) :
        uuid(uuid_),
        msg(msg_),
        rb(rb_)
    { }
    
    InputMapMsg(const InputMapMsg& m) :
        uuid(m.uuid),
        msg(m.msg),
        rb(m.rb)
    { }
    
    const UUID& get_uuid() const { return uuid; }
    const Message& get_msg() const { return msg; }
    
    // const gcomm::ReadBuf* get_rb() const { return rb; }
    gcomm::ReadBuf* get_rb() { return rb; }
private:
    void operator=(const InputMapMsg&);

    gcomm::UUID const uuid;
    Message  const msg;
    gcomm::ReadBuf* const rb;
};

class gcomm::evs::InputMapMsgIndex :
    public Map<const size_t, InputMapMsg>
{
};


/*!
 * Input map for messages.
 *
 */
class gcomm::evs::InputMap
{
public:
    /*!
     * Default constructor.
     */
    InputMap();

    /*!
     * Default destructor.
     */
    ~InputMap();
    
    /*!
     * Get current value of aru_seq.
     *
     * @return Current value of aru_seq;
     */
    Seqno get_aru_seq () const { return aru_seq;  }

    /*!
     *
     */
    Seqno get_safe_seq() const { return safe_seq; }
    
    /*!
     *
     */
    void  set_safe_seq(const UUID&, const Seqno);

    /*!
     *
     */
    Seqno get_safe_seq(const UUID&) const;

    /*!
     *
     */
    const Range& get_range   (const UUID&) const;
    
    typedef InputMapMsgIndex::iterator iterator;

    /*!
     *
     */
    iterator begin() const;

    /*!
     *
     */
    iterator end  () const;
    
    /*!
     *
     */
    bool is_safe  (iterator) const;
    
    /*!
     *
     */
    bool is_agreed(iterator) const;

    /*!
     *
     */
    bool is_fifo  (iterator) const;

    /*!
     *
     */
    Range insert(const UUID&, const Message&, 
                 const ReadBuf* rb = 0, size_t offset = 0);
    
    /*!
     *
     */
    void erase(iterator);
    
    /*!
     *
     */
    iterator find(const UUID&, const Seqno) const;
    
    /*!
     *
     */
    void insert_uuid(const UUID&);

    /*!
     * Clear input map state.
     */
    void clear();
    
private:
    /* Non-copyable */
    InputMap(const InputMap&);
    void operator=(const InputMap&);
    
    /*! 
     * Update aru_seq value to represent current state.
     */
    void update_aru();
    
    size_t get_index(const InputMapNode&, const Seqno) const;


    Seqno              safe_seq;       /*!< Safe seqno                  */
    Seqno              aru_seq;        /*!< All received upto seqno     */
    InputMapNodeIndex* node_index;     /*!< Index of evs nodes          */
    InputMapMsgIndex*  msg_index;      /*!< Index of messages           */
};

#endif // EVS_INPUT_MAP2_HPP
