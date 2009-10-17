/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file Input map for EVS messaging. Provides simple interface for 
 *       handling messages with different safety guarantees.
 *
 * @note When operating with iterators, note that evs::Message 
 *       accessed through iterator may have different sequence 
 *       number as it position dictates. Use sequence number 
 *       found from key part.
 *
 * @todo Fix issue in above note if feasible.
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
        /* Internal msg representation */
        class InputMapMsgKey
        {
        public:
            InputMapMsgKey(const size_t index_, const Seqno seq_) : 
                index(index_), 
                seq(seq_) 
            {}
    
            size_t get_index() const
            {
                return index;
            }
    
            Seqno get_seq() const
            {
                return seq;
            }
        
            bool operator<(const InputMapMsgKey& cmp) const
            {
                return (seq < cmp.seq || (seq == cmp.seq && index < cmp.index));
            }
        
        private:
            size_t const index;
            Seqno  const seq;
        };

        std::ostream& operator<<(std::ostream&, const InputMapMsgKey&);
        
        /* Internal message representation */
        class InputMapMsg
        {
        public:
            InputMapMsg(const gcomm::UUID& uuid_, const UserMessage& msg_, 
                gcomm::ReadBuf* const rb_) :
                uuid(uuid_),
                msg(msg_),
                rb(rb_)
            { 
            }
        
            InputMapMsg(const InputMapMsg& m) :
                uuid(m.uuid),
                msg(m.msg),
                rb(m.rb)
            { }
            
            const UUID& get_uuid() const { return uuid; }
            const UserMessage& get_msg() const { return msg; }
        
            const gcomm::ReadBuf* get_rb() const { return rb; }
            gcomm::ReadBuf* get_rb() { return rb; }
        private:
            void operator=(const InputMapMsg&);
        
            gcomm::UUID const uuid;
            UserMessage const msg;
            gcomm::ReadBuf* const rb;
        };
        
        std::ostream& operator<<(std::ostream&, const InputMapMsg&);

        class InputMapMsgIndex : public Map<InputMapMsgKey, InputMapMsg> { };
        
        std::ostream& operator<<(std::ostream&, const InputMapMsgIndex::value_type&);

        /* Internal node representation */
        class InputMapNode
        {
        public:
            InputMapNode() : idx(), range(0, Seqno::max()), safe_seq() { }
            void set_range(const Range& r) { range = r; }
            const Range& get_range() const { return range; }
            void set_safe_seq(const Seqno s) { safe_seq = s; }
            Seqno get_safe_seq() const { return safe_seq; }
            void set_index(const size_t i) { idx = i; }
            size_t get_index() const { return idx; }
        private:
            size_t idx;
            Range range;
            Seqno safe_seq;
        };


        std::ostream& operator<<(std::ostream&, const InputMapNode&);

        class InputMapNodeIndex : public gcomm::Map<gcomm::UUID, InputMapNode> { };

        /*!
         * Input map for messages.
         *
         */
        class InputMap
        {
        public:

            friend std::ostream& operator<<(std::ostream&, const InputMap&);
            
            typedef InputMapNode Node;

            /* Internal node index representation */
            typedef InputMapNodeIndex NodeIndex;

            typedef InputMapMsgKey MsgKey;
            typedef InputMapMsg Msg;
    
            /* Internal message index representation */
            typedef InputMapMsgIndex MsgIndex;

            /* Iterators exposed to user */
            typedef MsgIndex::iterator iterator;
            typedef MsgIndex::const_iterator const_iterator;
    
    
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
             * @return Current value of aru_seq
             */
            Seqno get_aru_seq () const { return aru_seq;  }

            /*!
             * Get current value of safe_seq. 
             *
             * @return Current value of safe_seq
             */
            Seqno get_safe_seq() const { return safe_seq; }
    
            /*!
             * Set sequence number safe for node.
             *
             * @param uuid Node uuid
             * @param seq Sequence number to be set safe
             *
             * @throws FatalException if node was not found or sequence number
             *         was not in the allowed range
             */
            void  set_safe_seq(const UUID& uuid, const Seqno seq);
    
            /*!
             * Get current value of safe_seq for node.
             *
             * @param uuid Node uuid
             *
             * @return Safe sequence number for node
             *
             * @throws FatalException if node was not found
             */
            Seqno get_safe_seq(const UUID& uuid) const;
    
            /*!
             * Get current range parameter for node
             *
             * @param uuid Node uuid
             * 
             * @return Range parameter for node
             *
             * @throws FatalException if node was not found
             */
            const Range& get_range   (const UUID& uuid) const;
    
    
    
            /*!
             * Get iterator to the beginning of the input map
             *
             * @return Iterator pointing to the first element
             */
            iterator begin() const;
    
            /*!
             * Get iterator next to the last element of the input map
             *
             * @return Iterator pointing past the last element
             */
            iterator end  () const;
    
            /*!
             * Check if message pointed by iterator fulfills SP_SAFE condition.
             *
             * @return True or false
             */
            bool is_safe  (iterator) const;
    
            /*!
             * Check if message pointed by iterator fulfills SP_AGREED condition.
             *
             * @return True or false
             */
            bool is_agreed(iterator) const;
    
            /*!
             * Check if message pointed by iterator fulfills SP_FIFO condition.
             *
             * @return True or false
             */
            bool is_fifo  (iterator) const;
    
            /*!
             * Insert new message into input map.
             *
             * @param uuid   Node uuid of the message source
             * @param msg    EVS message 
             * @param rb     ReadBuf pointer associated to message
             * @param offset Offset to the beginning of the payload
             *
             * @return Range parameter of the node
             *
             * @throws FatalException if node not found or message sequence 
             *         number is out of allowed range
             */
            Range insert(const UUID& uuid, const UserMessage& msg, 
                         const ReadBuf* rb = 0, size_t offset = 0);
    
            /*!
             * Erase message pointed by iterator. Note that message may still
             * be recovered through recover() method as long as it does not 
             * fulfill SP_SAFE constraint.
             *
             * @param i Iterator
             *
             * @throws FatalException if iterator is not valid
             */
            void erase(iterator i);
    
            /*!
             * Find message.
             *
             * @param uuid Message source node uuid
             * @param seq  Message sequence numeber
             *
             * @return Iterator pointing to message or at end() if message was not found
             *
             * @throws FatalException if node was not found
             */
            iterator find(const UUID& uuid, const Seqno seq) const;
    
            /*!
             * Recover message.
             *
             * @param uuid Message source node uuid
             * @param seq  Message sequence number
             *
             * @return Iterator pointing to the message
             *
             * @throws FatalException if node or message was not found
             */
            iterator recover(const UUID& uuid, const Seqno seq) const;
    
            /*!
             * Insert node uuid.
             *
             * @param uuid Node uuid
             *
             * @throws FatalException if uuid already exists or either message
             *         or recovery index is non empty.
             */
            void insert_uuid(const UUID& uuid);
    
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
    
            /*!
             * Clean up recovery index. All messages up to safe_seq are removed.
             */
            void cleanup_recovery_index();
    
    
    
            Seqno      safe_seq;       /*!< Safe seqno              */
            Seqno      aru_seq;        /*!< All received upto seqno */
            NodeIndex* node_index;     /*!< Index of nodes          */
            MsgIndex*  msg_index;      /*!< Index of messages       */
            MsgIndex*  recovery_index; /*!< Recovery index          */

            uint64_t   inserted;
            uint64_t   updated_aru;

        };

        std::ostream& operator<<(std::ostream&, const InputMap&);
    }
}
#endif // EVS_INPUT_MAP2_HPP
