#ifndef EVS_INPUT_MAP_HPP
#define EVS_INPUT_MAP_HPP

#include "gcomm/sockaddr.hpp"
#include "gcomm/readbuf.hpp"
#include "gcomm/logger.hpp"
#include "evs_message.hpp"
#include "evs_seqno.hpp"

#include <map>

struct EVSRange {
    uint32_t low;
    uint32_t high;
    EVSRange() : low(SEQNO_MAX), high(SEQNO_MAX) {}
};



class EVSInputMapItem {
    
    Sockaddr sa;
    EVSMessage msg;
    const ReadBuf* rb;
    size_t roff;
    static const Sockaddr null_sa;
    static const EVSMessage null_msg;
public:
    EVSInputMapItem(const Sockaddr& sa_, const EVSMessage& msg_, 
		    const ReadBuf* rb_, const size_t roff_ = 0) :
	sa(sa_), msg(msg_), rb(rb_), roff(roff_) {}

    EVSInputMapItem() : sa(null_sa), msg(null_msg), rb(0), roff(0){}



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

    struct MsgMapLstr {
	bool operator()(const uint32_t a, const uint32_t b) const {
	    return seqno_lt(a, b);
	}
    };
    
    typedef std::pair<const EVSMessage, ReadBuf*> Msg;
    typedef std::map<const uint32_t, Msg, MsgMapLstr> MsgMap;
    typedef std::pair<const uint32_t, Msg> MsgMapItem;

    struct Instance {
	MsgMap messages;
	EVSRange gap;
	uint32_t safe_seq;
	Instance() : safe_seq(SEQNO_MAX) {}
	~Instance() {
	    for (MsgMap::iterator mi = messages.begin(); mi != messages.end();
		 ++mi) {
		if (mi->second.second)
		    mi->second.second->release();
	    }
	}
    };
    
    typedef std::map<const Sockaddr, Instance> IMap;
    typedef std::pair<const Sockaddr, Instance> IMapItem;
    IMap instances;

    uint32_t safe_seq;
    uint32_t aru_seq;
public:
    EVSInputMap() : safe_seq(SEQNO_MAX), aru_seq(SEQNO_MAX) {
    }
    
    void set_safe(const Sockaddr& s, const uint32_t seq) {
	if (seqno_eq(aru_seq, SEQNO_MAX) || seqno_gt(seq, aru_seq))
	    throw FatalException("Seqno out of range");
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
    
    struct iterator {
	friend class EVSInputMap;
    private:
	const EVSInputMap* imp;
	EVSInputMapItem item;
	iterator(const EVSInputMap* imp_, const EVSInputMapItem& item_) :
	    imp(imp_), item(item_) {}
    public:
	iterator() : imp(0), item() {}
	bool operator!=(const iterator& cmp) const {
	    return !(item.get_sockaddr() == cmp.item.get_sockaddr() && 
		     seqno_eq(item.get_evs_message().get_seq(), 
			      cmp.item.get_evs_message().get_seq()));
	}
	iterator& operator++() {
	    return imp->next(*this);
	}
	const EVSInputMapItem& operator*() const {
	    return item;
	}
	const EVSInputMapItem* operator->() const {
	    return &item;
	}
    };
    

private:    
    iterator& next(iterator& iter) const {
	IMap::const_iterator ii = instances.find(iter->get_sockaddr());
	if (ii == instances.end())
	    throw FatalException("Instance not found");
	IMap::const_iterator ii_begin = ii;
	uint32_t seq = iter->get_evs_message().get_seq();
	size_t empty_cnt;
	do {
	    empty_cnt = 0;
	    do {
		++ii;
		if (ii == instances.end()) {
		    ii = instances.begin();
		    seq = seqno_next(seq);
		}
		
		// LOG_TRACE(std::string("\t") + ii->first.to_string() + " " + to_string(seq));
		if (ii->second.messages.empty() == false) {
		    MsgMap::const_iterator mi = ii->second.messages.find(seq);
		    if (mi != ii->second.messages.end()) {
			iter = iterator(this, EVSInputMapItem(ii->first, mi->second.first, mi->second.second));
			return iter;
		    } else if ((mi = ii->second.messages.upper_bound(seq)) == 
			       ii->second.messages.end()) {
			empty_cnt++;
		    } else {
			// TODO: Estimate next seqno using upper bound
		    }
		} else {
		    empty_cnt++;
		}
	    } while (ii != ii_begin);
	    LOG_TRACE(std::string("EVSInputMap::next(): Empty cnt = ") + to_string(empty_cnt));
	} while (empty_cnt != instances.size());
	LOG_TRACE("EVSInputMap::next(): end");
	return iter = end();
    }

    iterator end_tag;
    
public:
    iterator begin() const {
	uint32_t min_seq = SEQNO_MAX;
	IMap::const_iterator min_ii = instances.end();
	MsgMap::const_iterator min_mi;
	for (IMap::const_iterator ii = instances.begin(); 
	     ii != instances.end(); ++ii) {
	    if (ii->second.messages.empty() == false) {
		MsgMap::const_iterator mi = ii->second.messages.begin();
		if (seqno_eq(min_seq, SEQNO_MAX) || 
		    seqno_lt(mi->first, min_seq)) {
		    min_seq = mi->first;
		    min_ii = ii;
		    min_mi = mi;
		}
	    }
	}
	if (!seqno_eq(min_seq, SEQNO_MAX)) {
	    return iterator(this, EVSInputMapItem(min_ii->first, 
						  min_mi->second.first, 
						  min_mi->second.second));
	}
	return end();
    }

    
    const iterator& end() const {
	return end_tag;
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
	MsgMap& mmap(ii->second.messages);
	
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
	    std::pair<MsgMap::iterator, bool> iret;
	    if (seqno_eq(i, seq)) {
		iret = mmap.insert(
		    MsgMapItem(i, Msg(item.get_evs_message(), 
				      item.get_readbuf() ? 
				      item.get_readbuf()->copy(
					  item.get_readbuf_offset()
					  + item.get_evs_message().size()) : 0)));
	    } else {
		// TODO: Inserting dummy messages can probably be 
		// optimized away at some point.
		iret = mmap.insert(
		    MsgMapItem(i, Msg(EVSMessage(EVSMessage::USER,
						 EVSMessage::DROP,
						 i, 0,
						 item.get_evs_message().get_source_view(), 0), 0)));
	    }
	    if (iret.second) {
		LOG_TRACE(std::string("") + to_string(i));
		for (MsgMap::iterator mi = iret.first; mi != mmap.end(); ++mi) {
		    if ((seqno_eq(gap.low, SEQNO_MAX) && 
			 seqno_eq(mi->first, 0)) ||
			seqno_eq(gap.low, mi->first)) {
			gap.low = seqno_next(mi->first);
		    } else {
			break;
		    }
		}
		if (seqno_eq(gap.high, SEQNO_MAX) || seqno_gt(i, gap.high))
		    gap.high = i;
	    } else {
		// TODO: Sanity check
	    }
	}
	LOG_TRACE(std::string("EVSInputMap::insert(): ") 
		  + " aru_seq = " + to_string(aru_seq)
		  + " low = " + to_string(gap.low)
		  + " high = " + to_string(gap.high));
	if (seqno_eq(aru_seq, SEQNO_MAX) || seqno_gt(gap.low, aru_seq)) {
	    update_aru();
	}
	return EVSRange(gap);
    }
    
    void erase(const iterator& i) {
	IMap::iterator ii = instances.find(i->get_sockaddr());
	if (ii == instances.end())
	    throw FatalException("Instance not found");
	MsgMap::iterator mi = ii->second.messages.find(i->get_evs_message().get_seq());
	if (mi == ii->second.messages.end())
	    throw FatalException("Message not found");
	ii->second.messages.erase(mi);
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
    
    void clear() {
	if (instances.empty() == false)
	    instances.clear();
	safe_seq = aru_seq = SEQNO_MAX;
    }
};


#endif // EVS_INPUT_MAP
