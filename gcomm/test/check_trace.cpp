/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @brief Check trace implementation
 */

#include "check_trace.hpp"

using namespace std;
using namespace gu::net;
using namespace gcomm;


ostream& gcomm::operator<<(ostream& os, const TraceMsg& msg)
{
    return (os << "(" << msg.get_source() << "," << msg.get_source_view_id() << "," << msg.get_seq() << ")");
}

ostream& gcomm::operator<<(ostream& os, const ViewTrace& vtr)
{
    os << vtr.get_view() << ": ";
    copy(vtr.get_msgs().begin(), vtr.get_msgs().end(),
         ostream_iterator<const TraceMsg>(os, " "));
    return os;
}

ostream& gcomm::operator<<(ostream& os, const Trace& tr)
{
    os << "trace: \n";
    os << tr.get_view_traces();
    return os;
}

ostream& gcomm::operator<<(ostream& os, const Channel& ch)
{
    return (os << "(" << ch.get_latency() << "," << ch.get_loss() << ")");
}

ostream& gcomm::operator<<(ostream& os, const Channel* chp)
{
    return (os << *chp);
}

ostream& gcomm::operator<<(ostream& os, const MatrixElem& me)
{
    return (os << "(" << me.get_ii() << "," << me.get_jj() << ")");
}

ostream& gcomm::operator<<(ostream& os, const PropagationMatrix& prop)
{
    os << "(";
    copy(prop.prop.begin(), prop.prop.end(), 
         ostream_iterator<const ChannelMap::value_type>(os, ","));
    os << ")";
    return os;
}



class LinkOp
{
public:
    LinkOp(DummyNode& node_, ChannelMap& prop_) : 
        node(node_), prop(prop_) { }
    
    void operator()(NodeMap::value_type& l)
    {
        if (NodeMap::get_key(l) != node.get_index())
        {
            ChannelMap::iterator ii;
            gu_trace(ii = prop.insert_checked(
                         make_pair(MatrixElem(node.get_index(), 
                                              NodeMap::get_key(l)), 
                                   new Channel())));
            gcomm::connect(ChannelMap::get_value(ii), node.get_protos().front());
            gu_trace(ii = prop.insert_checked(
                         make_pair(MatrixElem(NodeMap::get_key(l),
                                              node.get_index()), 
                                   new Channel())));
            gcomm::connect(ChannelMap::get_value(ii), 
                           NodeMap::get_value(l)->get_protos().front());
        }
    }
private:
    DummyNode& node;
    ChannelMap& prop;
};



class PropagateOp
{
public:
    PropagateOp(NodeMap& tp_) : tp(tp_) { }
    
    void operator()(ChannelMap::value_type& vt)
    {
        ChannelMsg cmsg(vt.second->get());
        if (cmsg.get_rb().get_len() != 0)
        {
            NodeMap::iterator i(tp.find(vt.first.get_jj()));
            gcomm_assert(i != tp.end());
            gu_trace(NodeMap::get_value(i)->get_protos().front()->handle_up(
                         -1, cmsg.get_rb(),
                         ProtoUpMeta(cmsg.get_source())));
        }
    }
private:
    NodeMap& tp;
};


class ExpireTimersOp
{
public:
    ExpireTimersOp() { }
    void operator()(NodeMap::value_type& vt)
    {
        NodeMap::get_value(vt)->handle_timers();
    }
};

void gcomm::Channel::put(const Datagram& rb, const UUID& source) 
{ 
    Datagram dg(rb);
    if (dg.is_normalized() == false)
    {
        dg.normalize();
    }
    queue.push_back(make_pair(latency, ChannelMsg(dg, source)));
}

ChannelMsg gcomm::Channel::get()
{
    while (queue.empty() == false)
    {
        pair<size_t, ChannelMsg>& p(queue.front());
        if (p.first == 0)
        {
            // todo: packet loss goes here
            if (get_loss() < 1.)
            {
                double rnd(double(rand())/double(RAND_MAX));
                if (get_loss() < rnd)
                {
                    queue.pop_front();
                    return ChannelMsg(Datagram(), UUID::nil());
                }
            }
            ChannelMsg ret(p.second);
            queue.pop_front();
            return ret;
        }
        else
        {
            --p.first;
            return ChannelMsg(Datagram(), UUID::nil());
        }
    }
    return ChannelMsg(Datagram(), UUID::nil());
}

gcomm::PropagationMatrix::~PropagationMatrix()
{
    for_each(prop.begin(), prop.end(), ChannelMap::DeleteObject());
}

void gcomm::PropagationMatrix::insert_tp(DummyNode* t)
{
    gu_trace(tp.insert_checked(make_pair(t->get_index(), t)));
    for_each(tp.begin(), tp.end(), LinkOp(*t, prop));
}


void gcomm::PropagationMatrix::set_latency(const size_t ii, const size_t jj, 
                                           const size_t lat)
{
    ChannelMap::iterator i;
    gu_trace(i = prop.find_checked(MatrixElem(ii, jj)));
    ChannelMap::get_value(i)->set_latency(lat);
}


void gcomm::PropagationMatrix::set_loss(const size_t ii, const size_t jj, 
                                        const double loss)
{
    ChannelMap::iterator i;
    gu_trace(i = prop.find_checked(MatrixElem(ii, jj)));
    ChannelMap::get_value(i)->set_loss(loss);
}


void gcomm::PropagationMatrix::split(const size_t ii, const size_t jj)
{
    set_loss(ii, jj, 0.);
    set_loss(jj, ii, 0.);
}


void gcomm::PropagationMatrix::merge(const size_t ii, const size_t jj, const double loss)
{
    set_loss(ii, jj, loss);
    set_loss(jj, ii, loss);
}


void gcomm::PropagationMatrix::expire_timers()
{
    for_each(tp.begin(), tp.end(), ExpireTimersOp());
}


void gcomm::PropagationMatrix::propagate_n(size_t n)
{
    while (n-- > 0)
    {
        for_each(prop.begin(), prop.end(), PropagateOp(tp));
    }
}


void gcomm::PropagationMatrix::propagate_until_empty()
{
    do
    {
        for_each(prop.begin(), prop.end(), PropagateOp(tp));
    }
    while (count_channel_msgs() > 0);
}


void gcomm::PropagationMatrix::propagate_until_cvi(bool handle_timers)
{
    bool all_in = false;
    do
    {
        propagate_n(10);
        all_in = all_in_cvi();
        if (all_in == false && handle_timers == true)
        {
            expire_timers();
        }
    }
    while (all_in == false);
}


size_t gcomm::PropagationMatrix::count_channel_msgs() const
{
    size_t ret = 0;
    for (ChannelMap::const_iterator i = prop.begin(); 
         i != prop.end(); ++i)
    {
        ret += ChannelMap::get_value(i)->get_n_msgs();
    }
    return ret;
}


bool gcomm::PropagationMatrix::all_in_cvi() const
{
    for (map<size_t, DummyNode*>::const_iterator i = tp.begin(); 
         i != tp.end(); ++i)
    {
        if (i->second->in_cvi() == false)
        {
            return false;
        }
    }
    return true;
}



static void check_traces(const Trace& t1, const Trace& t2)
{
    for (Trace::ViewTraceMap::const_iterator 
             i = t1.get_view_traces().begin(); i != t1.get_view_traces().end();
         ++i)
    {
        Trace::ViewTraceMap::const_iterator i_next(i);
        ++i_next;
        if (i_next != t1.get_view_traces().end())
        {
            const Trace::ViewTraceMap::const_iterator 
                j(t2.get_view_traces().find(Trace::ViewTraceMap::get_key(i)));
            Trace::ViewTraceMap::const_iterator j_next(j);
            ++j_next;
            // Note: Comparision is meaningful if also next view is the 
            // same
            if (j             != t2.get_view_traces().end() && 
                j_next        != t2.get_view_traces().end() &&
                i_next->first == j_next->first          )
            {
                gcomm_assert(*i == *j) << 
                    "traces differ: " << *i << " != " << *j;
            }
        }
    }
}

class CheckTraceOp
{
public:
    CheckTraceOp(const vector<DummyNode*>& nvec_) : nvec(nvec_) { }
    
    void operator()(const DummyNode* n) const
    {
        for (vector<DummyNode*>::const_iterator i = nvec.begin(); 
             i != nvec.end();
             ++i)
        {
            if ((*i)->get_index() != n->get_index())
            {
                gu_trace(check_traces((*i)->get_trace(), n->get_trace()));
            }
        }
    }
private:
    const vector<DummyNode*>& nvec;
};


void gcomm::check_trace(const vector<DummyNode*>& nvec)
{
    for_each(nvec.begin(), nvec.end(), CheckTraceOp(nvec));
}
