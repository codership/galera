
#include "evs_input_map2.hpp"
#include "gcomm/readbuf.hpp"

#include <algorithm>

using namespace gcomm;
using namespace gcomm::evs;
using namespace std;

void release_rb(pair<const size_t, gcomm::evs::InputMapMsg>& p)
{
    ReadBuf* rb = p.second.get_rb();
    if (rb != 0)
    {
        rb->release();
    }
}




class gcomm::evs::InputMapNode
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

class gcomm::evs::InputMapNodeIndex :
    public gcomm::Map<const gcomm::UUID, InputMapNode>
{
};


gcomm::evs::InputMap::InputMap() :
    safe_seq(Seqno::max()),
    aru_seq(Seqno::max()),
    node_index(new InputMapNodeIndex()),
    msg_index(new InputMapMsgIndex())
{
}

gcomm::evs::InputMap::~InputMap()
{
    clear();
    delete node_index;
    delete msg_index;
}

bool gcomm::evs::InputMap::is_safe(iterator i) const
{
    const Seqno seq(InputMapMsgIndex::get_value(i).get_msg().get_seq());
    return (safe_seq != Seqno::max() && seq <= safe_seq);
}

bool gcomm::evs::InputMap::is_agreed(iterator i) const
{
    const Seqno seq(InputMapMsgIndex::get_value(i).get_msg().get_seq());
    return (aru_seq != Seqno::max() && seq <= aru_seq);
}

bool gcomm::evs::InputMap::is_fifo(iterator i) const
{
    const Seqno seq(InputMapMsgIndex::get_value(i).get_msg().get_seq());
    const InputMapNode& node(InputMapNodeIndex::get_value(
                                 node_index->find_checked(
                                     InputMapMsgIndex::get_value(i).get_uuid())));
    return (node.get_range().get_lu() > seq);
}


gcomm::evs::InputMap::iterator gcomm::evs::InputMap::begin() const
{
    return msg_index->begin();
}


gcomm::evs::InputMap::iterator gcomm::evs::InputMap::end() const
{
    return msg_index->end();
}

size_t gcomm::evs::InputMap::get_index(const InputMapNode& node,
                                       const Seqno seq) const
{
    return seq.get()*node_index->size() + node.get_index();
}

gcomm::evs::Range gcomm::evs::InputMap::insert(
    const UUID& uuid, 
    const Message& msg, 
    const ReadBuf* const rb, 
    const size_t offset)
{
    /* Only insert messages with meaningful seqno */
    gcomm_assert(msg.get_seq() != Seqno::max());
    
    InputMapNodeIndex::iterator node_i = node_index->find_checked(uuid);
    InputMapNode& node(InputMapNodeIndex::get_value(node_i));
    Range range(node.get_range());

    /* User should check aru_seq before inserting. This check is left 
     * also in optimized builds since violating it may cause duplicate 
     * messages */
    gcomm_assert(aru_seq == Seqno::max() || aru_seq < msg.get_seq()) 
        << "aru seq " << aru_seq << " msg seq " << msg.get_seq();
    
    /* User should check LU before inserting. This check is left 
     * also in optimized builds since violating it may cause duplicate 
     * messages */
    gcomm_assert(range.get_lu() <= msg.get_seq()) 
        << "lu " << range.get_lu() << " > "
        << msg.get_seq();
    
    /* Loop over message seqno range and insert messages when not 
     * already found */
    for (Seqno s = msg.get_seq(); s <= msg.get_seq() + msg.get_seq_range(); ++s)
    {
        size_t idx = get_index(node, s);
        InputMapMsgIndex::iterator msg_i = msg_index->find(idx);
        
        if (range.get_hs() == Seqno::max() || range.get_hs() >= s)
        {
            gcomm_assert(msg_i == msg_index->end());
        }
        
        if (msg_i == msg_index->end())
        {
            ReadBuf* ins_rb(0);
            if (s == msg.get_seq())
            {
                ins_rb = (rb != 0 ? rb->copy(offset) : 0);
            }
            (void)msg_index->insert_checked(
                make_pair(idx, InputMapMsg(uuid, msg, ins_rb)));
        }
        /* */
        if (range.get_hs() == Seqno::max() || range.get_hs() < s)
        {
            range.set_hs(s);
        }
        // log_info << "lu " << range.get_lu() << " s " << s;
        /* */
        if (range.get_lu() == s)
        {
            Seqno i = s;
            do
            {
                ++i;
                // log_info << "lu " << range.get_lu() << " i " << i;
            }
            while (msg_index->find(get_index(node, i)) != msg_index->end());
            range.set_lu(i);
        }
    }
    
    node.set_range(range);
    update_aru();
    return range;
}

void gcomm::evs::InputMap::erase(iterator i)
{
    if (is_safe(i) == true)
    {
        for_each(msg_index->begin(), i, release_rb);
        msg_index->erase(msg_index->begin(), i);
    }
}

gcomm::evs::InputMap::iterator gcomm::evs::InputMap::find(
    const UUID& uuid, const Seqno seq) const
{
    size_t idx = get_index(InputMapNodeIndex::get_value(node_index->find_checked(uuid)), seq);
    return msg_index->find(idx);
}


struct UpdateAruLUCmp
{
    bool operator()(const pair<const UUID, InputMapNode>& a,
                    const pair<const UUID, InputMapNode>& b) const
    {
        log_info << a.second.get_range().get_lu() << " "
                 << b.second.get_range().get_lu();
        return a.second.get_range().get_lu() < b.second.get_range().get_lu();
    }
};

void gcomm::evs::InputMap::update_aru()
{
    InputMapNodeIndex::const_iterator min = 
        min_element(node_index->begin(),
                    node_index->end(), UpdateAruLUCmp());
    
    const Seqno minval = InputMapNodeIndex::get_value(min).get_range().get_lu();
    log_info << "aru seq " << aru_seq << " next " << minval;
    if (aru_seq != Seqno::max())
    {
        /* aru_seq must not decrease */
        gcomm_assert(minval - 1 >= aru_seq);
        aru_seq = minval - 1;
    }
    else if (minval == 1)
    {
        aru_seq = 0;
    }
    
}

struct SetSafeSeqCmp
{

    bool operator()(const pair<const UUID, InputMapNode>& a,
                    const pair<const UUID, InputMapNode>& b) const    
    {
        if (a.second.get_safe_seq() == Seqno::max())
        {
            return true;
        }
        else if (b.second.get_safe_seq() == Seqno::max())
        {
            return false;
        }
        else
        {
            return a.second.get_safe_seq() < b.second.get_safe_seq();
        }
    }
};

void gcomm::evs::InputMap::set_safe_seq(const UUID& uuid, const Seqno seq)
{
    gcomm_assert(seq != Seqno::max());
    gcomm_assert(aru_seq != Seqno::max() && seq <= aru_seq);
    
    InputMapNode& node(InputMapNodeIndex::get_value(node_index->find_checked(uuid)));
    gcomm_assert(node.get_safe_seq() == Seqno::max() || 
                 seq >= node.get_safe_seq());
    node.set_safe_seq(seq);
    InputMapNodeIndex::const_iterator min = 
        min_element(node_index->begin(), node_index->end(), SetSafeSeqCmp());
    const Seqno minval = InputMapNodeIndex::get_value(min).get_safe_seq();
    gcomm_assert(safe_seq == Seqno::max() || minval >= safe_seq);
    safe_seq = minval;
}

void gcomm::evs::InputMap::insert_uuid(const UUID& uuid)
{
    gcomm_assert(msg_index->empty() == true);

    (void)node_index->insert_checked(make_pair(uuid, InputMapNode()));
    size_t n = 0;
    for (InputMapNodeIndex::iterator i = node_index->begin();
         i != node_index->end(); ++i)
    {
        InputMapNodeIndex::get_value(i).set_index(n);
        ++n;
    }
}



void gcomm::evs::InputMap::clear()
{
    for_each(msg_index->begin(), msg_index->end(), release_rb);
    msg_index->clear();
    node_index->clear();
    aru_seq = Seqno::max();
    safe_seq = Seqno::max();
}
