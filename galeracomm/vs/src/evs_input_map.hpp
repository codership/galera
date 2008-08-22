#ifndef EVS_INPUT_MAP_HPP
#define EVS_INPUT_MAP_HPP

#include "gcomm/sockaddr.hpp"
#include "gcomm/readbuf.hpp"
#include "gcomm/logger.hpp"
#include "evs_message.hpp"
#include "evs_seqno.hpp"

#include <map>
#include <set>


struct EVSRange {
    uint32_t low;
    uint32_t high;
    EVSRange() : low(SEQNO_MAX), high(SEQNO_MAX) {}
};



class EVSInputMapItem {
    
    Sockaddr sa;
    EVSMessage msg;
    const ReadBuf* rb;
    ReadBuf* priv_rb;
    size_t roff;
    static const Sockaddr null_sa;
    static const EVSMessage null_msg;
public:
    EVSInputMapItem(const Sockaddr& sa_, const EVSMessage& msg_, 
		    const ReadBuf* rb_, const size_t roff_) :
	sa(sa_), msg(msg_), rb(rb_), priv_rb(0), roff(roff_) {}
    
    EVSInputMapItem(const Sockaddr& sa_, const EVSMessage& msg_, 
		    ReadBuf* rb_) : sa(sa_), msg(msg_), rb(rb_), 
				    priv_rb(rb_), roff(0) {}
    
    EVSInputMapItem() : sa(null_sa), msg(null_msg), rb(0), priv_rb(0), roff(0){}

    ~EVSInputMapItem() {
	if (priv_rb)
	    priv_rb->release();
    }


    const Sockaddr& get_sockaddr() const {
	return sa;
    }
    const EVSMessage& get_evs_message() const {
	return msg;
    }
    const ReadBuf* get_readbuf() const {
	return rb;
    }
    const size_t get_readbuf_offset() const {
	return roff;
    }
    std::string to_string() const {
	return std::string("") + sa.to_string() + " "
	    + ::to_string(msg.get_seq());
    }
};

const Sockaddr EVSInputMapItem::null_sa = Sockaddr(0);
const EVSMessage EVSInputMapItem::null_msg = EVSMessage();

class EVSInputMap {
    // Map from sockaddr to instance index




    struct Instance {
	EVSRange gap;
	uint32_t safe_seq;
	Instance() : safe_seq(SEQNO_MAX) {}
	~Instance() {
	}

    };
    
    typedef std::map<const Sockaddr, Instance> IMap;
    typedef std::pair<const Sockaddr, Instance> IMapItem;
    IMap instances;


    struct MLogLstr {
	bool operator()(const EVSInputMapItem& a, const EVSInputMapItem& b) {
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
public:
    EVSInputMap() : safe_seq(SEQNO_MAX), aru_seq(SEQNO_MAX) {
    }
    
    void set_safe(const Sockaddr& s, const uint32_t seq) {
	if (seqno_eq(aru_seq, SEQNO_MAX) || seqno_gt(seq, aru_seq))
	    throw FatalException("Safe seqno out of range");
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
	    safe_seq = min_seq;
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
		  + " aru_seq = " + to_string(aru_seq) 
		  + " min_seq = " + to_string(min_seq));
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
	if (ii != instances.end())
	    throw FatalException("Instance not found");
	return !seqno_lt(ii->second.gap.low, i->get_evs_message().get_seq());
    }
    
    
    EVSRange insert(const EVSInputMapItem& item) {
	assert(item.get_evs_message().get_type() == EVSMessage::USER);
	// 
	IMap::iterator ii = instances.find(item.get_sockaddr());
	if (ii == instances.end())
	    throw FatalException("Instance not found");
	EVSRange& gap(ii->second.gap);
	uint32_t seq = item.get_evs_message().get_seq();
	uint8_t seq_range = item.get_evs_message().get_seq_range();
	
	uint32_t wseq = seqno_eq(aru_seq, SEQNO_MAX) ? 0 : aru_seq;
	if (seqno_gt(seq, seqno_add(wseq, SEQNO_MAX/4)) ||
	    seqno_lt(seq, seqno_dec(wseq, SEQNO_MAX/4))) {
	    LOG_WARN(std::string("Seqno out of window: ") + 
		     to_string(seq) + " current aru " + to_string(aru_seq));
	    return EVSRange(gap);
	}
	
	assert(!seqno_eq(gap.low, SEQNO_MAX) || seqno_eq(gap.high, SEQNO_MAX));

	
	for (uint32_t i = seq; !seqno_gt(i, seqno_add(seq, seq_range)); 
	     i = seqno_next(i)) {
	    std::pair<iterator, bool> iret;
	    if (seqno_eq(i, seq)) {
		iret = msg_log.insert(
		    EVSInputMapItem(item.get_sockaddr(), 
				    item.get_evs_message(),
				    item.get_readbuf() ? 
				    item.get_readbuf()->copy(
					item.get_readbuf_offset()) :
				    0));
		
	    } else {
		iret = msg_log.insert(
		    EVSInputMapItem(
			item.get_sockaddr(),
			EVSMessage(EVSMessage::USER,
				   EVSMessage::DROP,
				   i, 0, 
				   item.get_evs_message().get_aru_seq(),
				   item.get_evs_message().get_source_view(), 
				   0), 
			0));
	    }
	    if (iret.second) {
		if (seqno_eq(gap.high, SEQNO_MAX) || 
		    seqno_gt(i, gap.high)) {
		    gap.high = i;
		}
		if ((seqno_eq(gap.low, SEQNO_MAX) && seqno_eq(i, 0)) ||
		    seqno_eq(i, gap.low)) {
		    gap.low = seqno_next(gap.low);
		    if (!seqno_gt(gap.low, gap.high)) {
			MLog::iterator mi = iret.first;
			for (++mi; mi != msg_log.end(); ++mi) {
			    // Yes, this is not optimal, but this should
			    // be quite rare routine under considerably 
			    // small message loss
			    LOG_TRACE(std::string("\t") +
				      to_string(mi->get_evs_message().get_seq()) + " " + to_string(gap.low));
			    if (mi->get_sockaddr() == item.get_sockaddr()) {
				if (seqno_eq(mi->get_evs_message().get_seq(),
					     gap.low)) {
				    gap.low = seqno_next(gap.low);
				} else {
				    break;
				}
			    }
			    LOG_TRACE(std::string("\t") + to_string(gap.low));
			}
		    }
		} 
	    } else {
		// TODO: Sanity check
	    }
	}

#if 0
	for (MLog::iterator i = msg_log.begin(); i != msg_log.end(); ++i) {
	    LOG_TRACE(std::string("MLog: ") + i->get_sockaddr().to_string() + " " + to_string(i->get_evs_message().get_seq()));
	}
#endif 
	LOG_TRACE(std::string("EVSInputMap::insert(): ") 
		  + " aru_seq = " + to_string(aru_seq)
		  + " safe_seq = " + to_string(safe_seq)
		  + " low = " + to_string(gap.low)
		  + " high = " + to_string(gap.high));
	if (seqno_eq(aru_seq, SEQNO_MAX) || seqno_gt(gap.low, aru_seq)) {
	    update_aru();
	}
	if (!seqno_eq(item.get_evs_message().get_aru_seq(), SEQNO_MAX))
	    set_safe(ii->first, item.get_evs_message().get_aru_seq());
	return EVSRange(gap);
    }
    
    void erase(const iterator& i) {
	msg_log.erase(i);
    }

    

    void insert_sa(const Sockaddr& sa) {
	if (!seqno_eq(aru_seq, SEQNO_MAX))
	    throw FatalException("Can't add instance after aru has been updated");
	std::pair<IMap::iterator, bool> iret = 
	    instances.insert(IMapItem(sa, Instance()));
	if (iret.second == false)
	    throw FatalException("Instance already exists");
    }
    
    void erase_sa(const Sockaddr& sa) {
	IMap::iterator ii = instances.find(sa);
	if (ii == instances.end())
	    throw FatalException("Instance does not exist");
	instances.erase(sa);
    }

    EVSRange get_sa_gap(const Sockaddr& sa) const {
	IMap::const_iterator ii = instances.find(sa);
	if (ii == instances.end())
	    throw FatalException("Instance does not exist");
	return ii->second.gap;
    }
    
    void clear() {
	if (instances.empty() == false)
	    instances.clear();
	msg_log.clear();
	safe_seq = aru_seq = SEQNO_MAX;
    }
};


#endif // EVS_INPUT_MAP
