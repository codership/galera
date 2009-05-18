/*
 * VS protocol implementation over EVS
 */

#include "vs.hpp"
#include "evs_proto.hpp"
#include "vs_message.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/conf.hpp"
#include "gcomm/util.hpp"

#include <map>

#include <deque>
#include <limits>
#include <algorithm>

using std::map;
using std::deque;
using std::pair;
using std::includes;
using std::insert_iterator;
using std::set_intersection;

BEGIN_GCOMM_NAMESPACE

//
//
// VSProto
//
//


class VSInstance
{
    UUID pid;
    string name;
    uint32_t expected_seq;
    VSMessage* state_msg;
public:
    VSInstance(const UUID& pid_, const string& name_) :
        pid(pid_),
        name(name_),
        expected_seq(0),
        state_msg(0)
    {
        
    }
    
    ~VSInstance()
    {
        delete state_msg;
    }
    
    void set_expected_seq(uint32_t seq)
    {
        expected_seq = seq;
    }
    
    uint32_t get_expected_seq() const
    {
        return expected_seq;
    }

    void set_state_msg(VSMessage* msg)
    {
        if (state_msg != 0)
        {
            delete state_msg;
        }
        state_msg = msg;
    }
    
    const VSMessage* get_state_msg() const
    {
        return state_msg;
    }
};

typedef map<const UUID, VSInstance> VSInstMap;

class VSProto : public Protolay
{

    Monitor* mon;
public:
    enum State {JOINING, JOINED, LEAVING, LEFT} state;
    UUID addr;
    View *trans_view;
    View *reg_view;
    View *proposed_view;
    uint32_t next_seq;
    VSInstMap membs;

    static const UUID& get_pid(VSInstMap::iterator i)
    {
        return i->first;
    }

    static VSInstance& get_instance(VSInstMap::iterator i)
    {
        return i->second;
    }

    class TransQueueMsg
    {
        ReadBuf* rb;
        size_t roff;
        UUID source;
    public:
        TransQueueMsg(const ReadBuf* rb_, const size_t roff_, 
                      const UUID& source_) :
            rb(rb_->copy(roff_)), roff(0), source(source_)
        {
            
        }

        TransQueueMsg(const TransQueueMsg& msg) :
            rb(msg.get_readbuf()->copy()),
            roff(msg.get_roff()),
            source(msg.get_source())
        {
        }
        
        ~TransQueueMsg()
        {
            rb->release();
        }

        const ReadBuf* get_readbuf() const
        {
            return rb;
        }

        size_t get_roff() const
        {
            return roff;
        }
        
        const UUID& get_source() const
        {
            return source;
        }

    };

    typedef deque<TransQueueMsg> TransQueue;
    TransQueue trans_msgs;
    

    VSProto(const UUID& a, Monitor* mon_) : 
        mon(mon_),
        state(JOINING),
	addr(a), 
        trans_view(0), 
        reg_view(0), 
	proposed_view(0), 
        next_seq(0)
    {
    }
    
    ~VSProto() {
	delete trans_view;
	delete reg_view;
	delete proposed_view;
        membs.clear();
	trans_msgs.clear();
    }
    void deliver_data(const VSMessage *, const ReadBuf *, const size_t roff);
    void handle_conf(const View*);
    void handle_state(const VSMessage *);
    void handle_data(const VSMessage *, const ReadBuf *, const size_t);
    
    void handle_up(const int cid, const ReadBuf* rb, const size_t roff, 
                   const ProtoUpMeta* um)
    {
        Critical crit(mon);

        VSMessage msg;

        if (rb == 0 && um->get_view() == 0) {
            throw FatalException("broken backend connection");
        }
        
        if (um->get_view())
        {
            LOG_INFO("VS: " + um->get_view()->to_string());
            handle_conf(um->get_view());
            return;
        }
        
        if (msg.read(rb->get_buf(), rb->get_len(), roff) == 0)
        {
            LOG_FATAL("failed to read message");
            throw FatalException("failed to read message");
        }
        LOG_TRACE("VSProto::handle_up(): msg type = " 
                  + Int(msg.get_type()).to_string() 
                  + " from " + um->get_source().to_string());
        msg.set_source(um->get_source());
        switch (msg.get_type()) {
        case VSMessage::T_STATE:
            handle_state(&msg);
            break;
        case VSMessage::T_DATA:
            handle_data(&msg, rb, roff);
            break;
        default:
            LOG_WARN("unknown message type, dropping");
        }
    }
    int handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
    {
        Critical crit(mon);

        uint8_t user_type = dm ? dm->get_user_type() : 0xff;
        
        if (reg_view == 0)
        {
            LOG_WARN(std::string("VS::handle_down(): ") + strerror(ENOTCONN));
            return ENOTCONN;
        }
        
        
        // Transitional configuration
        if (trans_view)
        {
            LOG_DEBUG(std::string("VS::handle_down() trans conf: ") + strerror(EAGAIN));
            return EAGAIN;
        }
        
        VSInstMap::iterator mi = membs.find(addr);
        if (mi == membs.end())
        {
            throw FatalException("VS::handle_down(): Internal error");
        }
        if (get_instance(mi).get_expected_seq() + 256 == next_seq)
        {
            LOG_INFO("VS::handle_down(), flow control: " 
                     + UInt32(get_instance(mi).get_expected_seq()).to_string() 
                     + " " + UInt32(next_seq).to_string());
            return EAGAIN;
        }
        
        VSDataMessage msg(reg_view->get_id(), next_seq, user_type);
        
        char hdrbuf[28];
        size_t hdrbuflen;
        if ((hdrbuflen = msg.write(hdrbuf, sizeof(hdrbuf), 0)) == 0)
        {
            LOG_FATAL("out of buffer space, needed " + Size(msg.size()).to_string());
            throw FatalException("out of buffer space");
        }
        wb->prepend_hdr(hdrbuf, hdrbuflen);
        
        int ret = pass_down(wb, 0);
        if (ret == 0) {
            LOG_TRACE(std::string("Sent message ") + UInt32(next_seq).to_string());
            next_seq++;
        }
        wb->rollback_hdr(hdrbuflen);
        if (ret)
        {
            LOG_DEBUG(string("VS::handle_down(), returning ") 
                      + strerror(ret));
        }
        return ret;
    }
    
};



void VSProto::handle_conf(const View* const view)
{

    if (state == LEAVING && view->is_empty()) 
    {
        LOG_INFO("LEAVING");
        // TODO: Check that no messages missing (i.e. EVS self delivery is 
        // fulfilled)
	ProtoUpMeta um(view);
	pass_up(0, 0, &um);
	state = LEFT;
	return;
    }
    
    /* Iterate over view members and add to membs list */
    for (NodeList::const_iterator i = view->get_members().begin();
         i != view->get_members().end(); ++i)
    {
        if (membs.find(get_uuid(i)) == membs.end())
        {
            LOG_INFO("Adding new member: " + get_uuid(i).to_string() + ":" + get_name(i));
            if (membs.insert(make_pair(get_uuid(i), 
                                       VSInstance(get_uuid(i), get_name(i)))).second == false)
            {
                LOG_FATAL("Failed to add instance: " + get_uuid(i).to_string() + ":" + get_name(i));
                throw FatalException("failed to add instance");
            }
        }
    }
    
    if (trans_view == 0 && reg_view == 0) {
        if (view->get_type() != View::V_TRANS)
        {
            throw FatalException("");
        }
	if (proposed_view != 0)
        {
	    throw FatalException("");
        }
	trans_view = new View(*view);
    } else if (view->get_type() == View::V_TRANS) {
	delete proposed_view;
	proposed_view = 0;
	for (VSInstMap::iterator i = membs.begin(); 
	     i != membs.end(); ++i) {
            get_instance(i).set_state_msg(0);
	}
	View& curr_view(trans_view ? *trans_view : *reg_view);
        
        NodeList addr_inter;
	set_intersection(curr_view.get_members().begin(), 
                         curr_view.get_members().end(), 
                         view->get_members().begin(), 
                         view->get_members().end(),
                         insert_iterator<NodeList>(
                             addr_inter, addr_inter.begin()));
	trans_view = 0;
	
	trans_view = new View(View::V_TRANS, reg_view ? 
				reg_view->get_id() : 
				ViewId());
	trans_view->add_members(addr_inter.begin(), addr_inter.end());
	if (reg_view && includes(reg_view->get_members().begin(),
                                 reg_view->get_members().end(),
                                 trans_view->get_members().begin(),
                                 trans_view->get_members().end()) == false )
        {
	    throw FatalException("");
	} 
    } else {
	if (trans_view == 0 || proposed_view != 0)
	    throw FatalException("");
	proposed_view = new View(View::V_REG, view->get_id());
	proposed_view->add_members(view->get_members().begin(), view->get_members().end());
	VSStateMessage state_msg(proposed_view->get_id(), 
                                 *proposed_view, 
                                 next_seq);
        
        LOG_INFO("VS: sending state message with view " + proposed_view->to_string());
        
	unsigned char *buf = new unsigned char[state_msg.size()];
	if (state_msg.write(buf, state_msg.size(), 0) == 0)
        {
            throw FatalException("failed to serialize message");
        }
	WriteBuf wb(buf, state_msg.size());
	int err = pass_down(&wb, 0);
	if (err)
        { 
	    // Fixme: poll until there's space
	    throw FatalException("");
	} 
        else if (err != 0)
        {
	    throw FatalException(strerror(errno));
	} 
	delete[] buf;
    }
}

void VSProto::deliver_data(const VSMessage *dm, 
			   const ReadBuf *rb, const size_t roff)
{
    VSInstMap::iterator membs_p = membs.find(dm->get_source());
    if (membs_p == membs.end())
    {
        LOG_FATAL("message source " + dm->get_source().to_string() + " not in memb");
	throw FatalException("message source not in memb"); 
    }
    if (get_instance(membs_p).get_expected_seq() != dm->get_seq())
    {
        LOG_FATAL("gap in message sequence, expected " + UInt32(get_instance(membs_p).get_expected_seq()).to_string() + " got " + UInt32(dm->get_seq()).to_string());
        throw FatalException("gap in message sequence");
    }
    get_instance(membs_p).set_expected_seq(dm->get_seq() + 1);
    LOG_TRACE("Delivering message " + UInt32(dm->get_seq()).to_string());
    ProtoUpMeta um(dm->get_source(), dm->get_user_type());
    pass_up(rb, roff + dm->size(), &um);
}


void VSProto::handle_state(const VSMessage *sm)
{
    if (proposed_view == 0 || 
	proposed_view->get_id() != sm->get_source_view()) {
	if (trans_view) 
        {
            LOG_WARN("Cascading view changes, dropping state message");
	    return;
	} 
        else 
        {
	    // This should be impossible:
	    // - If proposed view exists, there must exist transitional 
	    //   configuration we are part of
	    // - If state message is originated from different backend 
	    //   reg_view, there must have been at least one intermediate
	    //   backend transitional view and thus there must exist 
	    //   VS::trans_view (VS::trans_view is dropped only after 
	    //   VS::reg_view is installed after receiving all state messages)
	    throw FatalException("");
	}
    }
    
    VSInstMap::iterator i;
    if ((i = membs.find(sm->get_source())) == membs.end()) {
        throw FatalException("");
#if 0
        pair<VSInstMap::iterator, bool> iret =
            membs.insert(make_pair(
			     sm->get_source(),
                             VSInstance(sm->get_source())));
        if (iret.second == false)
        {
            throw FatalException("");
        }
        i = iret.first;
#endif // 0
    } 
    
    VSInstance& memb(i->second);
    
    if (memb.get_state_msg()) {
	throw FatalException("");
    }
    
    memb.set_state_msg(new VSMessage(*sm));
    size_t n_state_msgs = 0;
    for (i = membs.begin(); i != membs.end(); ++i) 
    {
	if (get_instance(i).get_state_msg() != 0)
	    n_state_msgs++;
    }

    LOG_INFO("proposed view: " + proposed_view->to_string());
    LOG_INFO("state msgs: " + UInt32(n_state_msgs).to_string());
    if (n_state_msgs == proposed_view->get_members().length()) 
    {
        LOG_INFO("received all state messages");
	if (trans_msgs.size() && reg_view == 0)
        {
	    throw FatalException("Trans msgs without preceding reg view");
        }
	state = JOINED;
        LOG_INFO("deliver view: " + trans_view->to_string());
	ProtoUpMeta um(trans_view);
	pass_up(0, 0, &um);
	for (TransQueue::iterator tm = trans_msgs.begin();
	     trans_msgs.empty() == false; tm = trans_msgs.begin()) {
	    const ReadBuf* rb = tm->get_readbuf();
	    size_t roff = tm->get_roff();
            const UUID& source = tm->get_source();
	    VSMessage dm;
	    if (dm.read(rb->get_buf(), rb->get_len(), roff) == 0) {
		throw FatalException("");
	    }
	    dm.set_source(source);
	    deliver_data(&dm, rb, roff);
	    trans_msgs.pop_front();
	}
	
	// Erase members that have left
	VSInstMap::iterator i_next;
	for (i = membs.begin(); i != membs.end(); i = i_next) {
	    i_next = i, ++i_next;
	    if (proposed_view->get_members().find(get_pid(i)) == 
		proposed_view->get_members().end()) {
		membs.erase(i);
	    }
	}
	
	// This must be done in the case of network partition/merge
	for (i = membs.begin(); i != membs.end(); ++i) {
	    if (get_instance(i).get_expected_seq() != 
                get_instance(i).get_state_msg()->get_seq() &&
		trans_view->get_members().find(get_pid(i)) != 
                trans_view->get_members().end()) {
                LOG_FATAL(get_pid(i).to_string() 
                          + " expected seq: " 
                          + UInt32(get_instance(i).get_expected_seq()).to_string()
                          + " state_msg seq: " 
                          + UInt32(get_instance(i).get_state_msg()->get_seq()).to_string());
		throw FatalException("");
	    }
            get_instance(i).set_expected_seq(get_instance(i).get_state_msg()->get_seq());
	}
	
	delete trans_view;
	trans_view = 0;
	
	delete reg_view;
	reg_view = proposed_view;
	proposed_view = 0;
        
        LOG_INFO("deliver view: " + reg_view->to_string());
	ProtoUpMeta umr(reg_view);
	pass_up(0, 0, &umr);
    }
}

void VSProto::handle_data(const VSMessage *dm, const ReadBuf *rb, 
                          const size_t roff)
{
    if (reg_view == 0)
	return;
    
    // Verify send view correctness 
    if (dm->get_source_view() != reg_view->get_id())
    {
	throw FatalException("");
    }

    // Transitional configuration
    if (trans_view)
    {
        trans_msgs.push_back(TransQueueMsg(rb, roff, dm->get_source()));
    }
    else 
    {
        deliver_data(dm, rb, roff);
    }
}

//
//
// VS
//
//


void VS::connect()
{
    
    URI tp_uri = uri;
    tp_uri.set_scheme(Conf::GMCastScheme);
    tp = Transport::create(tp_uri, event_loop);
    tp->connect();
    UUID uuid(tp->get_uuid());
    if (uuid == UUID())
    {
        LOG_FATAL("invalid UUID: " + uuid.to_string());
        throw FatalException("invalid UUID");
    }
    string name;
    URIQueryList::const_iterator i = 
        uri.get_query_list().find(Conf::NodeQueryName);
    if (i == uri.get_query_list().end())
    {
        name = uuid.to_string();
    }
    else
    {
        name = get_query_value(i);
    }
    
    evs_proto = new EVSProto(event_loop, tp, uuid, name, mon);
    tp->set_up_context(evs_proto);
    evs_proto->set_down_context(tp);
    proto = new VSProto(uuid, mon);
    evs_proto->set_up_context(proto);
    proto->set_down_context(evs_proto);
    proto->set_up_context(this);
    set_down_context(proto);

    // boot up EVS proto
    evs_proto->shift_to(EVSProto::JOINING);
    Time stop(Time::now() + Time(5, 0));
    if ((i = uri.get_query_list().find(Conf::EvsQueryJoinWait)) != uri.get_query_list().end())
    {
        long tval = read_long(get_query_value(i));
        stop = Time(Time::now() + Time(tval/1000, (tval % 1000)*1000));
    }
    
    do 
    {
        int ret = event_loop->poll(50);
        LOG_DEBUG(string("poll returned ") + Int(ret).to_string());
    } 
    while (stop >= Time::now() && evs_proto->known.size() == 1);
    LOG_INFO("EVS Proto initial state: " + evs_proto->to_string());
    LOG_INFO("EVS Proto sending join request");
    evs_proto->send_join();
}


void VS::close()
{
    if (tp == 0)
    {
        LOG_FATAL("vs not connected");
        throw FatalException("");
    }
    LOG_INFO("VS Proto leaving");
    proto->state = VSProto::LEAVING;
    evs_proto->shift_to(EVSProto::LEAVING);
    LOG_INFO("VS Proto sending leave notification");
    evs_proto->send_leave();
    do
    {
        int ret = event_loop->poll(50);
        if (ret < 0)
        {
            LOG_WARN("poll returned: " + Int(ret).to_string());
        }
    }
    while (evs_proto->get_state() != EVSProto::CLOSED);
    if (proto->state != VSProto::LEFT)
    {
        LOG_WARN("VS didn't terminate properly");
    }
    tp->close();
    delete tp;
    tp = 0;
    delete evs_proto;
    evs_proto = 0;
    delete proto;
    proto = 0;
}


void VS::handle_up(const int cid, 
                   const ReadBuf *rb, 
                   const size_t roff, 
                   const ProtoUpMeta *um)
{
    pass_up(rb, roff, um);
}



int VS::handle_down(WriteBuf *wb, const ProtoDownMeta *dm)
{
    return pass_down(wb, dm);
}

size_t VS::get_max_msg_size() const
{
    if (tp == 0)
    {
        return 1024;
    }
    else
    {
        EVSUserMessage evsm(UUID(), SAFE, 0, 0, 0, ViewId(UUID(), 0), 0);
        VSDataMessage vsm(ViewId(UUID(), 0), 0, 0);
        if (tp->get_max_msg_size() < evsm.size() + vsm.size())
        {
            LOG_FATAL("transport max msg size too small: " +
                      Size(tp->get_max_msg_size()).to_string());
            throw FatalException("");
        }
        return tp->get_max_msg_size() - evsm.size() - vsm.size();
    }
}

bool VS::supports_uuid() const
{
    return true;
}

const UUID& VS::get_uuid() const
{
    return tp->get_uuid();
}

VS::VS(const URI& uri_, EventLoop* event_loop_, Monitor* mon_) : 
    Transport(uri_, event_loop_, mon_)
{
}


VS::~VS()
{
    // TODO
}

END_GCOMM_NAMESPACE
