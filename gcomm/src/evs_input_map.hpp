#ifndef EVS_INPUT_MAP_HPP
#define EVS_INPUT_MAP_HPP

#include "gcomm/common.hpp"
#include "gcomm/readbuf.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/uuid.hpp"
#include "evs_message.hpp"
#include "evs_seqno.hpp"

#include <map>
#include <set>

BEGIN_GCOMM_NAMESPACE

class EVSInputMapItem
{

    UUID sa;
    EVSMessage msg;
    ReadBuf* rb;
    size_t roff;
public:
    EVSInputMapItem(const UUID& sa_, const EVSMessage& msg_, 
                    const ReadBuf* rb_, const size_t roff_) :
        sa(sa_), 
        msg(msg_), 
        rb(rb_ ? rb_->copy() : 0), 
        roff(roff_) 
    {
    }
    
    EVSInputMapItem(const EVSInputMapItem& i)
    {
        *this = i;
        if (i.rb)
            rb = i.rb->copy();
    }
    
    EVSInputMapItem() : sa(UUID()), msg(EVSMessage()), rb(0), roff(0){}
    
    ~EVSInputMapItem() 
    {
        if (rb)
            rb->release();
    }
    
    const UUID& get_sockaddr() const
    {
        return sa;
    }

    const EVSMessage& get_evs_message() const
    {
        return msg;
    }
    
    const ReadBuf* get_readbuf() const
    {
        return rb;
    }
    
    size_t get_readbuf_offset() const
    {
        return roff;
    }
    
    size_t get_payload_offset() const {
        return roff + msg.size();
    }
    
    string to_string() const
    {
        return sa.to_string() + " " + UInt32(msg.get_seq()).to_string();
    }
};


class EVSInputMap {
    // Map from sockaddr to instance index
    
    struct Instance {
        EVSRange gap;
        uint32_t safe_seq;
        Instance() : safe_seq(SEQNO_MAX) {}
        ~Instance() {
        }
        
    };
    
    typedef std::map<const UUID, Instance> IMap;
    typedef std::pair<const UUID, Instance> IMapItem;
    IMap instances;
    
    
    struct MLogLstr {
        bool operator()(const EVSInputMapItem& a, const EVSInputMapItem& b) const {
            if (seqno_lt(a.get_evs_message().get_seq(), 
                         b.get_evs_message().get_seq()))
                return true;
            else if (seqno_gt(a.get_evs_message().get_seq(), 
                              b.get_evs_message().get_seq()))
                return false;
            return a.get_sockaddr() < b.get_sockaddr();;
        }
    };
    
    typedef std::set<EVSInputMapItem, MLogLstr> MLog;
    MLog recovery_log;
    MLog msg_log;
    
    uint32_t safe_seq;
    uint32_t aru_seq;
    
// Some stats
    uint64_t n_messages;
    uint64_t msg_log_size_cum;
    uint64_t recovery_log_size_cum;
    
public:
    EVSInputMap() : safe_seq(SEQNO_MAX), aru_seq(SEQNO_MAX),
                    n_messages(0), msg_log_size_cum(0), 
                    recovery_log_size_cum(0) {
    }
    
    ~EVSInputMap() {
        LOG_INFO("~EVSInputMap(): "
                 + Double(double(msg_log_size_cum)/double(n_messages + 1)).to_string() 
                 + " "
                 + Double(double(recovery_log_size_cum)/
                          double(n_messages + 1)).to_string());
    }
    
    void set_safe(const UUID& s, const uint32_t seq) {
        //if (seqno_eq(aru_seq, SEQNO_MAX) || seqno_gt(seq, aru_seq))
        //    throw FatalException("Safe seqno out of range");
        IMap::iterator ii = instances.find(s);
        if (ii == instances.end())
            throw FatalException("Instance not found");
        
        if (seqno_eq(ii->second.safe_seq, SEQNO_MAX) || 
            seqno_lt(ii->second.safe_seq, seq))
            ii->second.safe_seq = seq;
        if (seqno_eq(safe_seq, SEQNO_MAX) || seqno_lt(safe_seq, seq)) {
            uint32_t min_seq = seq;
            for (ii = instances.begin(); 
                 ii != instances.end() && !seqno_eq(min_seq, SEQNO_MAX); ++ii) {
                if (!seqno_eq(ii->second.safe_seq, SEQNO_MAX)) {
                    min_seq = seqno_lt(min_seq, ii->second.safe_seq) ? 
                        min_seq : ii->second.safe_seq;
                } else {
                    min_seq = SEQNO_MAX;
                }
            }
            if (!seqno_eq(safe_seq, min_seq)) {
                assert(seqno_eq(safe_seq, SEQNO_MAX) || 
                       seqno_gt(min_seq, safe_seq));
                safe_seq = min_seq;
                MLog::iterator i_next;
                for (MLog::iterator i = recovery_log.begin(); 
                     i != recovery_log.end() && 
                         !seqno_gt(i->get_evs_message().get_seq(), safe_seq);
                     i = i_next) {
                    i_next = i;
                    ++i_next;
                    recovery_log.erase(i);
                }
            }
        }
    }
    
    uint32_t get_safe_seq() const {
        return safe_seq;
    }
    
    void update_aru() {
        if (instances.empty())
            throw FatalException("Instance not found");
        uint32_t min_seq = SEQNO_MAX;
        for (IMap::iterator ii = instances.begin(); ii != instances.end(); ++ii) {
            // Aru must always be less or equal than minimum of gap.lows
            assert(seqno_eq(aru_seq, SEQNO_MAX) || 
                   seqno_eq(ii->second.gap.low, SEQNO_MAX) ||
                   !seqno_lt(ii->second.gap.low, aru_seq));
            if (seqno_eq(ii->second.gap.low, SEQNO_MAX)) {
                assert(seqno_eq(aru_seq, SEQNO_MAX));
                min_seq = SEQNO_MAX;
                break;
            } else if (seqno_eq(min_seq, SEQNO_MAX) || 
                       seqno_lt(ii->second.gap.low, min_seq)) {
                min_seq = ii->second.gap.low;
            } 
        }
        // Aru is actually one less than min_seq computed from gap lows
        // since gap low points to next seq of smallest received seq
        if (!seqno_eq(min_seq, SEQNO_MAX))
            min_seq = seqno_dec(min_seq, 1);
        // Aru must not decrease during update
        assert(seqno_eq(aru_seq, SEQNO_MAX) || !seqno_lt(min_seq, aru_seq));
        LOG_TRACE(std::string("EVSInputMap::update_aru()") 
                  + " aru_seq = " + UInt32(aru_seq).to_string() 
                  + " min_seq = " + UInt32(min_seq).to_string());
        aru_seq = min_seq;
    }
    
    uint32_t get_aru_seq() const {
        return aru_seq;
    }
    
    
    typedef MLog::iterator iterator;
    iterator begin() {
        return msg_log.begin();
    }
    iterator end() {
        return msg_log.end();
    }
    
    bool is_safe(const iterator& i) const {
        return !(seqno_eq(safe_seq, SEQNO_MAX) || seqno_lt(safe_seq, i->get_evs_message().get_seq()));
    }
    
    bool is_agreed(const iterator& i) const {
        return !(seqno_eq(aru_seq, SEQNO_MAX) || seqno_lt(aru_seq, i->get_evs_message().get_seq()));
    }
    
    bool is_fifo(const iterator& i) const {
        IMap::const_iterator ii = instances.find(i->get_sockaddr());
        if (ii == instances.end())
        {
            LOG_FATAL("instance " + i->get_sockaddr().to_string() 
                + " not found");
            throw FatalException("Instance not found");
        }
        if (seqno_eq(ii->second.gap.low, SEQNO_MAX))
        {
            return false;
        }
        LOG_INFO("is_fifo: " + make_int(ii->second.gap.low).to_string() 
                 + " " + make_int(i->get_evs_message().get_seq()).to_string());
        return !seqno_lt(ii->second.gap.low, i->get_evs_message().get_seq());
    }

    std::list<EVSRange> get_gap_list(const UUID& pid) const {
        IMap::const_iterator ii = instances.find(pid);
        if (ii == instances.end())
            throw FatalException("Instance not found");
        std::list<EVSRange> lst;
        assert(!seqno_eq(ii->second.gap.get_high(), SEQNO_MAX));
        uint32_t start_seq = seqno_eq(ii->second.gap.get_low(), SEQNO_MAX) ? 
            0 : ii->second.gap.get_low();
        
        for (uint32_t seq = start_seq; !seqno_gt(seq, ii->second.gap.get_high());
             seq = seqno_next(seq)) {
            EVSInputMapItem tmp(pid, EVSUserMessage(
                                    pid,
                                    DROP, 
                                    seq, 0, 0, ViewId(), 0), 0, 0);
            MLog::iterator i;
            if ((i = msg_log.find(tmp)) == msg_log.end() && 
                (i = recovery_log.find(tmp)) == recovery_log.end()) {
                EVSRange r(seq, seq);
                lst.push_back(r);
            }
        }
        return lst;
    }
    
    
    /*!
     * Insert new input map item. Message contained by item can be 
     * either USER or LEAVE message. 
     *
     * @param item Input map item
     * @return Range corresponding to source seqno range of the instance. 
     */
    EVSRange insert(const EVSInputMapItem& item);
    
    void erase(const iterator& i) {
        if (is_safe(i) == false) {
            std::pair<MLog::iterator, bool> iret = recovery_log.insert(*i);
            assert(iret.second == true);
            recovery_log_size_cum += recovery_log.size();
        }
        msg_log.erase(i);
    }
    
    std::pair<EVSInputMapItem, bool> 
    recover(const UUID& sa, const uint32_t seq) const {
        EVSInputMapItem tmp(sa, EVSUserMessage(
                                sa,
                                DROP, 
                                seq, 0, 0, ViewId(), 0), 0, 0);
        MLog::iterator i;
        if ((i = msg_log.find(tmp)) == msg_log.end() && 
            (i = recovery_log.find(tmp)) == recovery_log.end()) {
                    
            return std::pair<EVSInputMapItem, bool>(EVSInputMapItem(), false);
        }
        return std::pair<EVSInputMapItem, bool>(*i, true);
    }
    
    
    bool contains_sa(const UUID& sa) const {
        IMap::const_iterator ii = instances.find(sa);
        return !(ii == instances.end());
    }
    
    void insert_sa(const UUID& sa) {
        if (!seqno_eq(aru_seq, SEQNO_MAX))
            throw FatalException("Can't add instance after aru has been updated");
        std::pair<IMap::iterator, bool> iret = 
            instances.insert(IMapItem(sa, Instance()));
        if (iret.second == false)
            throw FatalException("Instance already exists");
    }
    
    void erase_sa(const UUID& sa) {
        IMap::iterator ii = instances.find(sa);
        if (ii == instances.end())
            throw FatalException("Instance does not exist");
        instances.erase(sa);
    }
    
    EVSRange get_sa_gap(const UUID& sa) const {
        IMap::const_iterator ii = instances.find(sa);
        if (ii == instances.end())
            throw FatalException("Instance does not exist");
        return ii->second.gap;
    }
    
    void clear() {
        if (instances.empty() == false)
            instances.clear();
        if (msg_log.size())
        {
            LOG_WARN("going to discard " 
                     + Size(msg_log.size()).to_string() 
                     + " messages from msg log");
            for (MLog::const_iterator i = msg_log.begin(); i != msg_log.end();
                 ++i)
            {
                LOG_WARN("source " + i->get_sockaddr().to_string() + " seq " 
                         + UInt32(i->get_evs_message().get_seq()).to_string());
            }
        }
        
        msg_log.clear();
        if (recovery_log.size())
        {
            LOG_INFO("going to discard " 
                     + Size(recovery_log.size()).to_string() 
                     + " messages from recovery log");
            for (MLog::const_iterator i = recovery_log.begin(); i != recovery_log.end();
                 ++i)
            {
                LOG_INFO("source " + i->get_sockaddr().to_string() + " seq " + UInt32(i->get_evs_message().get_seq()).to_string());
            }
        }
        recovery_log.clear();
        safe_seq = aru_seq = SEQNO_MAX;
    }
};

END_GCOMM_NAMESPACE


#endif // EVS_INPUT_MAP
