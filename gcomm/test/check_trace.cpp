/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @brief Check trace implementation
 */

#include "check_trace.hpp"
#include "gcomm/conf.hpp"

using namespace std;
using namespace gu;
using namespace gcomm;

struct CheckTraceConfInit
{
    explicit CheckTraceConfInit(gu::Config& conf)
    {
        gcomm::Conf::register_params(conf);
    }
};

// This is to avoid static initialization fiasco with gcomm::Conf static members
// Ideally it is the latter which should be wrapped in a function, but, unless
// this is used to initialize another static object, it should be fine.
gu::Config& check_trace_conf()
{
    static gu::Config conf;
    static CheckTraceConfInit const check_trace_conf_init(conf);

    return conf;
}

ostream& gcomm::operator<<(ostream& os, const TraceMsg& msg)
{
    return (os << "(" << msg.source() << "," << msg.source_view_id() << ","
            << msg.seq() << ")");
}

ostream& gcomm::operator<<(ostream& os, const ViewTrace& vtr)
{
    os << vtr.view() << ": ";
    copy(vtr.msgs().begin(), vtr.msgs().end(),
         ostream_iterator<const TraceMsg>(os, " "));
    return os;
}

ostream& gcomm::operator<<(ostream& os, const Trace& tr)
{
    os << "trace: \n";
    os << tr.view_traces();
    return os;
}

ostream& gcomm::operator<<(ostream& os, const Channel& ch)
{
    return (os << "(" << ch.latency() << "," << ch.loss() << ")");
}

ostream& gcomm::operator<<(ostream& os, const Channel* chp)
{
    return (os << *chp);
}

ostream& gcomm::operator<<(ostream& os, const MatrixElem& me)
{
    return (os << "(" << me.ii() << "," << me.jj() << ")");
}

ostream& gcomm::operator<<(ostream& os, const PropagationMatrix& prop)
{
    os << "(";
    copy(prop.prop_.begin(), prop.prop_.end(),
         ostream_iterator<const ChannelMap::value_type>(os, ","));
    os << ")";
    return os;
}



class LinkOp
{
public:
    LinkOp(DummyNode& node, ChannelMap& prop) :
        node_(node), prop_(prop) { }

    void operator()(NodeMap::value_type& l)
    {
        if (NodeMap::key(l) != node_.index())
        {
            ChannelMap::iterator ii;
            gu_trace(ii = prop_.insert_unique(
                         make_pair(MatrixElem(node_.index(),
                                              NodeMap::key(l)),
                                   new Channel(check_trace_conf()))));
            gcomm::connect(ChannelMap::value(ii), node_.protos().front());
            gu_trace(ii = prop_.insert_unique(
                         make_pair(MatrixElem(NodeMap::key(l),
                                              node_.index()),
                                   new Channel(check_trace_conf()))));
            gcomm::connect(ChannelMap::value(ii),
                           NodeMap::value(l)->protos().front());
        }
    }
private:
    DummyNode& node_;
    ChannelMap& prop_;
};



class PropagateOp
{
public:
    PropagateOp(NodeMap& tp) : tp_(tp) { }

    void operator()(ChannelMap::value_type& vt)
    {
        ChannelMsg cmsg(vt.second->get());
        if (cmsg.rb().len() != 0)
        {
            NodeMap::iterator i(tp_.find(vt.first.jj()));
            gcomm_assert(i != tp_.end());
            gu_trace(NodeMap::value(i)->protos().front()->handle_up(
                         &tp_, cmsg.rb(),
                         ProtoUpMeta(cmsg.source())));
        }
    }
private:
    NodeMap& tp_;
};


class ExpireTimersOp
{
public:
    ExpireTimersOp() { }
    void operator()(NodeMap::value_type& vt)
    {
        NodeMap::value(vt)->handle_timers();
    }
};

void gcomm::Channel::put(const Datagram& rb, const UUID& source)
{
    Datagram dg(rb);
//    if (dg.is_normalized() == false)
    //  {
    //   dg.normalize();
    // }
    queue_.push_back(make_pair(latency_, ChannelMsg(dg, source)));
}

ChannelMsg gcomm::Channel::get()
{
    while (queue_.empty() == false)
    {
        pair<size_t, ChannelMsg>& p(queue_.front());
        if (p.first == 0)
        {
            // todo: packet loss goes here
            if (loss() < 1.)
            {
                double rnd(double(rand())/double(RAND_MAX));
                if (loss() < rnd)
                {
                    queue_.pop_front();
                    return ChannelMsg(Datagram(), UUID::nil());
                }
            }
            ChannelMsg ret(p.second);
            queue_.pop_front();
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
    for_each(prop_.begin(), prop_.end(), ChannelMap::DeleteObject());
}

void gcomm::PropagationMatrix::insert_tp(DummyNode* t)
{
    gu_trace(tp_.insert_unique(make_pair(t->index(), t)));
    for_each(tp_.begin(), tp_.end(), LinkOp(*t, prop_));
}


void gcomm::PropagationMatrix::set_latency(const size_t ii, const size_t jj,
                                           const size_t lat)
{
    ChannelMap::iterator i;
    gu_trace(i = prop_.find_checked(MatrixElem(ii, jj)));
    ChannelMap::value(i)->set_latency(lat);
}


void gcomm::PropagationMatrix::set_loss(const size_t ii, const size_t jj,
                                        const double loss)
{
    ChannelMap::iterator i;
    gu_trace(i = prop_.find_checked(MatrixElem(ii, jj)));
    ChannelMap::value(i)->set_loss(loss);
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
    for_each(tp_.begin(), tp_.end(), ExpireTimersOp());
}


void gcomm::PropagationMatrix::propagate_n(size_t n)
{
    while (n-- > 0)
    {
        for_each(prop_.begin(), prop_.end(), PropagateOp(tp_));
    }
}


void gcomm::PropagationMatrix::propagate_until_empty()
{
    do
    {
        for_each(prop_.begin(), prop_.end(), PropagateOp(tp_));
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
    for (ChannelMap::const_iterator i = prop_.begin();
         i != prop_.end(); ++i)
    {
        ret += ChannelMap::value(i)->n_msgs();
    }
    return ret;
}


bool gcomm::PropagationMatrix::all_in_cvi() const
{
    for (map<size_t, DummyNode*>::const_iterator i = tp_.begin();
         i != tp_.end(); ++i)
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
             i = t1.view_traces().begin(); i != t1.view_traces().end();
         ++i)
    {
        Trace::ViewTraceMap::const_iterator i_next(i);
        ++i_next;
        if (i_next != t1.view_traces().end())
        {
            const Trace::ViewTraceMap::const_iterator
                j(t2.view_traces().find(Trace::ViewTraceMap::key(i)));
            Trace::ViewTraceMap::const_iterator j_next(j);
            ++j_next;
            // Note: Comparision is meaningful if also next view is the
            //       same.
            // @todo Proper checks for PRIM and NON_PRIM
            if (j             != t2.view_traces().end() &&
                j_next        != t2.view_traces().end() &&
                i_next->first == j_next->first              &&
                i_next->second.view().members() ==
                j_next->second.view().members())
            {
                if (i->first.type() != V_NON_PRIM &&
                    i->first.type() != V_PRIM)
                {
                    gcomm_assert(*i == *j)
                        << "traces differ: \n\n" << *i << "\n\n" << *j << "\n\n"
                        << "next views: \n\n" << *i_next << "\n\n" << *j_next;
                }
                else
                {
                    // todo
                }
            }
        }
    }
}

class CheckTraceOp
{
public:
    CheckTraceOp(const vector<DummyNode*>& nvec) : nvec_(nvec) { }

    void operator()(const DummyNode* n) const
    {
        for (vector<DummyNode*>::const_iterator i = nvec_.begin();
             i != nvec_.end();
             ++i)
        {
            if ((*i)->index() != n->index())
            {
                gu_trace(check_traces((*i)->trace(), n->trace()));
            }
        }
    }
private:
    const vector<DummyNode*>& nvec_;
};


void gcomm::check_trace(const vector<DummyNode*>& nvec)
{
    for_each(nvec.begin(), nvec.end(), CheckTraceOp(nvec));
}

