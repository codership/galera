/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * Classes for tracing views and messages
 */

#include "gu_network.hpp"
#include "gu_uri.hpp"
#include "gu_datetime.hpp"

#include "gcomm/uuid.hpp"
#include "gcomm/protolay.hpp"
#include "gcomm/protostack.hpp"
#include "gcomm/transport.hpp"
#include "gcomm/map.hpp"
#include "gcomm/util.hpp"

#include <vector>
#include <deque>
#include <functional>

extern gu::Config check_trace_conf;


namespace gcomm
{
    class TraceMsg
    {
    public:
        TraceMsg(const UUID& source_           = UUID::nil(),
                 const ViewId& source_view_id_ = ViewId(),
                 const int64_t seq_            = -1) :
            source(source_),
            source_view_id(source_view_id_),
            seq(seq_)
        { }

        const UUID& get_source() const { return source; }

        const ViewId& get_source_view_id() const { return source_view_id; }

        int64_t get_seq() const { return seq; }

        bool operator==(const TraceMsg& cmp) const
        {
            return (source         == cmp.source         &&
                    source_view_id == cmp.source_view_id &&
                    seq            == cmp.seq              );

        }

    private:
        UUID    source;
        ViewId  source_view_id;
        int64_t seq;
    };

    std::ostream& operator<<(std::ostream& os, const TraceMsg& msg);

    class ViewTrace
    {
    public:
        ViewTrace(const View& view_) : view(view_), msgs() { }

        void insert_msg(const TraceMsg& msg)
            throw (gu::Exception)
        {
            switch (view.get_type())
            {
            case V_REG:
                gcomm_assert(view.get_id() == msg.get_source_view_id());
                gcomm_assert(contains(msg.get_source()) == true)
                    << "msg source " << msg.get_source() << " not int view "
                    << view;
                break;
            case V_TRANS:
                gcomm_assert(view.get_id().get_uuid() ==
                             msg.get_source_view_id().get_uuid() &&
                             view.get_id().get_seq() ==
                             msg.get_source_view_id().get_seq());
                break;
            case V_NON_PRIM:
                break;
            case V_PRIM:
                gcomm_assert(view.get_id() == msg.get_source_view_id())
                    << " view id " << view.get_id()
                    <<  " source view " << msg.get_source_view_id();
                gcomm_assert(contains(msg.get_source()) == true);
                break;
            case V_NONE:
                gu_throw_fatal;
                break;
            }

            if (view.get_type() != V_NON_PRIM)
            {
                msgs.push_back(msg);
            }
        }

        const View& get_view() const { return view; }

        const std::deque<TraceMsg>& get_msgs() const { return msgs; }

        bool operator==(const ViewTrace& cmp) const
        {
            // Note: Cannot compare joining members since seen differently
            // on different merging subsets
            return (view.get_members()     == cmp.view.get_members()     &&
                    view.get_left()        == cmp.view.get_left()        &&
                    view.get_partitioned() == cmp.view.get_partitioned() &&
                    msgs                   == cmp.msgs                     );
        }
    private:

        bool contains(const UUID& uuid) const
        {
            return (view.get_members().find(uuid) != view.get_members().end() ||
                    view.get_left().find(uuid)    != view.get_left().end() ||
                    view.get_partitioned().find(uuid) != view.get_partitioned().end());
        }

        View       view;
        std::deque<TraceMsg> msgs;
    };

    std::ostream& operator<<(std::ostream& os, const ViewTrace& vtr);


    class Trace
    {
    public:
        class ViewTraceMap : public Map<ViewId, ViewTrace> { };

        Trace() : views(), current_view(views.end()) { }

        void insert_view(const View& view)
        {
            gu_trace(current_view = views.insert_unique(
                         std::make_pair(view.get_id(), ViewTrace(view))));

            log_debug << view;
        }
        void insert_msg(const TraceMsg& msg)
        {
            gcomm_assert(current_view != views.end()) << "no view set before msg delivery";
            gu_trace(ViewTraceMap::get_value(current_view).insert_msg(msg));
        }
        const ViewTraceMap& get_view_traces() const { return views; }

        const ViewTrace& get_current_view_trace() const
        {
            gcomm_assert(current_view != views.end());
            return ViewTraceMap::get_value(current_view);
        }

    private:
        ViewTraceMap views;
        ViewTraceMap::iterator current_view;
    };


    std::ostream& operator<<(std::ostream& os, const Trace& tr);

    class DummyTransport : public Transport
    {
        UUID uuid;
        std::deque<gu::Datagram*> out;
        bool queue;
    public:
        DummyTransport(const UUID& uuid_ = UUID::nil(), bool queue_ = true,
                       const gu::URI& uri = gu::URI("dummy:")) :
            Transport(*std::auto_ptr<Protonet>(Protonet::create(check_trace_conf)),
                      uri),
            uuid(uuid_),
            out(),
            queue(queue_)
        {}

        ~DummyTransport()
        {
            out.clear();
        }

        bool supports_uuid() const { return true; }

        const UUID& get_uuid() const { return uuid; }


        size_t get_mtu() const { return (1U << 31); }

        void connect(bool first) { }

        void close() { }
        void close(const UUID&) { }

        void connect() { }

        void listen()
        {
            gu_throw_fatal << "not implemented";
        }

        Transport *accept()
        {
            gu_throw_fatal << "not implemented";
            return 0;
        }

        void handle_up(const void* cid, const gu::Datagram& rb,
                       const ProtoUpMeta& um)
        {
            send_up(rb, um);
        }

        int handle_down(gu::Datagram& wb, const ProtoDownMeta& dm)
        {
            if (queue == true)
            {
                // assert(wb.get_header().size() == 0);
                out.push_back(new gu::Datagram(wb));
                return 0;
            }
            else
            {
                gu_trace(return send_down(wb, ProtoDownMeta(0xff, O_UNRELIABLE, uuid)));
            }
        }

        gu::Datagram* get_out()
        {
            if (out.empty())
            {
                return 0;
            }
            gu::Datagram* rb = out.front();
            out.pop_front();
            return rb;
        }
    };


    class DummyNode : public Toplay
    {
    public:
        DummyNode(gu::Config& conf,
                  const size_t index_,
                  const std::list<Protolay*>& protos_) :
            Toplay (conf),
            index  (index_),
            uuid   (UUID(static_cast<int32_t>(index))),
            protos (protos_),
            cvi    (),
            tr     (),
            curr_seq(0)
        {
            gcomm_assert(protos.empty() == false);
            std::list<Protolay*>::iterator i, i_next;
            i = i_next = protos.begin();
            for (++i_next; i_next != protos.end(); ++i, ++i_next)
            {
                gu_trace(gcomm::connect(*i, *i_next));
            }
            gu_trace(gcomm::connect(*i, this));
        }

        ~DummyNode()
        {
            std::list<Protolay*>::iterator i, i_next;
            i = i_next = protos.begin();
            for (++i_next; i_next != protos.end(); ++i, ++i_next)
            {
                gu_trace(gcomm::disconnect(*i, *i_next));
            }
            gu_trace(gcomm::disconnect(*i, this));
            std::for_each(protos.begin(), protos.end(), gu::DeleteObject());
        }


        const UUID& get_uuid() const { return uuid; }

        std::list<Protolay*>& get_protos() { return protos; }

        size_t get_index() const { return index; }

        void connect(bool first)
        {
            gu_trace(std::for_each(protos.rbegin(), protos.rend(),
                                   std::bind2nd(
                                       std::mem_fun(&Protolay::connect), first)));
        }

        void close()
        {
            for (std::list<Protolay*>::iterator i = protos.begin();
                 i != protos.end(); ++i)
            {
                (*i)->close();
            }
            // gu_trace(std::for_each(protos.rbegin(), protos.rend(),
            //                       std::mem_fun(&Protolay::close)));
        }


        void close(const UUID& uuid)
        {
            for (std::list<Protolay*>::iterator i = protos.begin();
                 i != protos.end(); ++i)
            {
                (*i)->close(uuid);
            }
            // gu_trace(std::for_each(protos.rbegin(), protos.rend(),
            //                       std::mem_fun(&Protolay::close)));
        }

        void send()
        {
            const int64_t seq(curr_seq);
            gu::byte_t buf[sizeof(seq)];
            size_t sz;
            gu_trace(sz = serialize(seq, buf, sizeof(buf), 0));
            gu::Datagram dg(gu::Buffer(buf, buf + sz));
            int err = send_down(dg, ProtoDownMeta(0));
            if (err != 0)
            {
                log_debug << "failed to send: " << strerror(err);
            }
            else
            {
                ++curr_seq;
            }
        }

        const Trace& get_trace() const { return tr; }

        void set_cvi(const ViewId& vi)
        {
            log_debug << get_uuid() << " setting cvi to " << vi;
            cvi = vi;
        }

        bool in_cvi() const
        {
            for (Trace::ViewTraceMap::const_reverse_iterator i(
                     tr.get_view_traces().rbegin());
                 i != tr.get_view_traces().rend(); ++i)
            {
                if (i->first.get_uuid() == cvi.get_uuid() &&
                    i->first.get_type() == cvi.get_type() &&
                    i->first.get_seq()  >= cvi.get_seq())
                {
                    return true;
                }
            }
            return false;
        }

        void handle_up(const void* cid, const gu::Datagram& rb,
                       const ProtoUpMeta& um)
        {
            if (rb.get_len() != 0)
            {
                gcomm_assert((um.get_source() == UUID::nil()) == false);
                // assert(rb.get_header().size() == 0);
                const gu::byte_t* begin(get_begin(rb));
                const size_t available(get_available(rb));


                // log_debug << um.get_source() << " " << get_uuid()
                //         << " " << available ;
                // log_debug << rb.get_len() << " " << rb.get_offset() << " "
                //         << rb.get_header_len();
                if (available != 8)
                {
                    log_info << "check_trace fail";
                }
                gcomm_assert(available == 8);
                int64_t seq;
                gu_trace(gcomm::unserialize(begin,
                                            available,
                                            0,
                                            &seq));
                tr.insert_msg(TraceMsg(um.get_source(), um.get_source_view_id(),
                                       seq));
            }
            else
            {
                gcomm_assert(um.has_view() == true);
                tr.insert_view(um.get_view());
            }
        }


        gu::datetime::Date handle_timers()
        {
            std::for_each(protos.begin(), protos.end(),
                          std::mem_fun(&Protolay::handle_timers));
            return gu::datetime::Date::max();
        }

    private:
        size_t index;
        UUID uuid;
        std::list<Protolay*> protos;
        ViewId cvi;
        Trace tr;
        int64_t curr_seq;
    };



    class ChannelMsg
    {
    public:
        ChannelMsg(const gu::Datagram& rb_, const UUID& source_) :
            rb(rb_),
            source(source_)
        {
        }
        const gu::Datagram& get_rb() const { return rb; }
        const UUID& get_source() const { return source; }
    private:
        gu::Datagram rb;
        UUID source;
    };


    class Channel : public Bottomlay
    {
    public:
        Channel(gu::Config& conf,
                const size_t ttl_ = 1,
                const size_t latency_ = 1,
                const double loss_ = 1.) :
            Bottomlay(conf),
            ttl(ttl_),
            latency(latency_),
            loss(loss_),
            queue()
        { }



        ~Channel() { }

        int handle_down(gu::Datagram& wb, const ProtoDownMeta& dm)
        {
            gcomm_assert((dm.get_source() == UUID::nil()) == false);
            gu_trace(put(wb, dm.get_source()));
            return 0;
        }

        void put(const gu::Datagram& rb, const UUID& source);
        ChannelMsg get();
        void set_ttl(const size_t t) { ttl = t; }
        size_t get_ttl() const { return ttl; }
        void set_latency(const size_t l)
        {
            gcomm_assert(l > 0);
            latency = l;
        }
        size_t get_latency() const { return latency; }
        void set_loss(const double l) { loss = l; }
        double get_loss() const { return loss; }
        size_t get_n_msgs() const
        {
            return queue.size();
        }
    private:
        size_t ttl;
        size_t latency;
        double loss;
        std::deque<std::pair<size_t, ChannelMsg> > queue;
    };


    std::ostream& operator<<(std::ostream& os, const Channel& ch);
    std::ostream& operator<<(std::ostream& os, const Channel* ch);




    class MatrixElem
    {
    public:
        MatrixElem(const size_t ii_, const size_t jj_) : ii(ii_), jj(jj_) { }
        size_t get_ii() const { return ii; }
        size_t get_jj() const { return jj; }
        bool operator<(const MatrixElem& cmp) const
        {
            return (ii < cmp.ii || (ii == cmp.ii && jj < cmp.jj));
        }
    private:
        size_t ii;
        size_t jj;
    };

    std::ostream& operator<<(std::ostream& os, const MatrixElem& me);

    class ChannelMap : public  Map<MatrixElem, Channel*>
    {
    public:
        struct DeleteObject
        {
            void operator()(ChannelMap::value_type& vt)
            {
                delete ChannelMap::get_value(vt);
            }
        };
    };
    class NodeMap : public Map<size_t, DummyNode*> {
    public:
        struct DeleteObject
        {
            void operator()(NodeMap::value_type& vt)
            {
                delete NodeMap::get_value(vt);
            }
        };

    };

    class PropagationMatrix
    {
    public:
        PropagationMatrix() : tp(), prop() { }
        ~PropagationMatrix();

        void insert_tp(DummyNode* t);
        void set_latency(const size_t ii, const size_t jj, const size_t lat);
        void set_loss(const size_t ii, const size_t jj, const double loss);
        void split(const size_t ii, const size_t jj);
        void merge(const size_t ii, const size_t jj, const double loss = 1.0);
        void propagate_n(size_t n);
        void propagate_until_empty();
        void propagate_until_cvi(bool handle_timers);
        friend std::ostream& operator<<(std::ostream&, const PropagationMatrix&);
    private:
        void expire_timers();


        size_t count_channel_msgs() const;
        bool all_in_cvi() const;

        NodeMap    tp;
        ChannelMap prop;
    };


    std::ostream& operator<<(std::ostream& os, const PropagationMatrix& prop);

    // Cross check traces from vector of dummy nodes
    void check_trace(const std::vector<DummyNode*>& nvec);

} // namespace gcomm
