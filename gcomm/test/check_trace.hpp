/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * Classes for tracing views and messages
 */

#include "gu_uri.hpp"
#include "gu_datetime.hpp"

#include "gcomm/datagram.hpp"
#include "gcomm/uuid.hpp"
#include "gcomm/protolay.hpp"
#include "gcomm/protostack.hpp"
#include "gcomm/transport.hpp"
#include "gcomm/map.hpp"
#include "gcomm/util.hpp"

#include <vector>
#include <deque>
#include <functional>

gu::Config& check_trace_conf();

extern "C" void check_trace_log_cb(int, const char*);

namespace gcomm
{
    class TraceMsg
    {
    public:
        TraceMsg(const UUID& source           = UUID::nil(),
                 const ViewId& source_view_id = ViewId(),
                 const int64_t seq            = -1) :
            source_(source),
            source_view_id_(source_view_id),
            seq_(seq)
        { }

        const UUID& source() const { return source_; }

        const ViewId& source_view_id() const { return source_view_id_; }

        int64_t seq() const { return seq_; }

        bool operator==(const TraceMsg& cmp) const
        {
            return (source_         == cmp.source_         &&
                    source_view_id_ == cmp.source_view_id_ &&
                    seq_            == cmp.seq_              );

        }

    private:
        UUID    source_;
        ViewId  source_view_id_;
        int64_t seq_;
    };

    std::ostream& operator<<(std::ostream& os, const TraceMsg& msg);

    class ViewTrace
    {
    public:
        ViewTrace(const View& view) : view_(view), msgs_() { }

        void insert_msg(const TraceMsg& msg)
        {
            switch (view_.type())
            {
            case V_REG:
                gcomm_assert(view_.id() == msg.source_view_id());
                gcomm_assert(contains(msg.source()) == true)
                    << "msg source " << msg.source() << " not int view "
                    << view_;
                break;
            case V_TRANS:
                gcomm_assert(view_.id().uuid() ==
                             msg.source_view_id().uuid() &&
                             view_.id().seq() ==
                             msg.source_view_id().seq());
                break;
            case V_NON_PRIM:
                break;
            case V_PRIM:
                gcomm_assert(view_.id() == msg.source_view_id())
                    << " view id " << view_.id()
                    <<  " source view " << msg.source_view_id();
                gcomm_assert(contains(msg.source()) == true);
                break;
            case V_NONE:
                gu_throw_fatal;
                break;
            }

            if (view_.type() != V_NON_PRIM)
            {
                msgs_.push_back(msg);
            }
        }

        const View& view() const { return view_; }

        const std::deque<TraceMsg>& msgs() const { return msgs_; }

        bool operator==(const ViewTrace& cmp) const
        {
            // Note: Cannot compare joining members since seen differently
            // on different merging subsets
            return (view_.members()     == cmp.view_.members()     &&
                    view_.left()        == cmp.view_.left()        &&
                    view_.partitioned() == cmp.view_.partitioned() &&
                    msgs_              == cmp.msgs_                     );
        }
    private:

        bool contains(const UUID& uuid) const
        {
            return (view_.members().find(uuid) != view_.members().end() ||
                    view_.left().find(uuid)    != view_.left().end() ||
                    view_.partitioned().find(uuid) !=view_.partitioned().end());
        }

        View       view_;
        std::deque<TraceMsg> msgs_;
    };

    std::ostream& operator<<(std::ostream& os, const ViewTrace& vtr);


    class Trace
    {
    public:
        class ViewTraceMap : public Map<ViewId, ViewTrace> { };

        Trace() : views_(), current_view_(views_.end()) { }

        void insert_view(const View& view)
        {
            gu_trace(current_view_ = views_.insert_unique(
                         std::make_pair(view.id(), ViewTrace(view))));

            log_debug << view;
        }
        void insert_msg(const TraceMsg& msg)
        {
            gcomm_assert(current_view_ != views_.end())
                << "no view set before msg delivery";
            gu_trace(ViewTraceMap::value(current_view_).insert_msg(msg));
        }
        const ViewTraceMap& view_traces() const { return views_; }

        const ViewTrace& current_view_trace() const
        {
            gcomm_assert(current_view_ != views_.end());
            return ViewTraceMap::value(current_view_);
        }

    private:
        ViewTraceMap views_;
        ViewTraceMap::iterator current_view_;
    };


    std::ostream& operator<<(std::ostream& os, const Trace& tr);

    class DummyTransport : public Transport
    {
        UUID uuid_;
        std::deque<Datagram*> out_;
        bool queue_;
        static std::unique_ptr<Protonet> net_;
        Protonet& get_net();
    public:

        DummyTransport(const UUID& uuid = UUID::nil(), bool queue = true,
                       const gu::URI& uri = gu::URI("dummy:")) :
            Transport(get_net(), uri),
            uuid_(uuid),
            out_(),
            queue_(queue)
        {}

        ~DummyTransport()
        {
            out_.clear();
        }

        const UUID& uuid() const { return uuid_; }

        size_t mtu() const { return (1U << 31); }

        void connect(bool first) { }

        void close(bool force) { }
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

        void handle_up(const void* cid, const Datagram& rb,
                       const ProtoUpMeta& um)
        {
            send_up(rb, um);
        }

        void set_queueing(bool val) { queue_ = val; }

        int handle_down(Datagram& wb, const ProtoDownMeta& dm)
        {
            if (queue_ == true)
            {
                // assert(wb.header().size() == 0);
                out_.push_back(new Datagram(wb));
                return 0;
            }
            else
            {
                gu_trace(return send_down(wb, ProtoDownMeta(0xff, O_UNRELIABLE, uuid_)));
            }
        }

        bool empty() const { return out_.empty(); }

        Datagram* out()
        {
            if (out_.empty())
            {
                return 0;
            }
            Datagram* rb = out_.front();
            out_.pop_front();
            return rb;
        }
    };


    class DummyNode : public Toplay
    {
    public:
        DummyNode(gu::Config& conf,
                  const size_t index,
                  const gcomm::UUID& uuid,
                  const std::list<Protolay*>& protos) :
            Toplay (conf),
            index_  (index),
            uuid_   (uuid),
            protos_ (protos),
            cvi_    (),
            tr_     (),
            curr_seq_(0)
        {
            gcomm_assert(protos_.empty() == false);
            std::list<Protolay*>::iterator i, i_next;
            i = i_next = protos_.begin();
            for (++i_next; i_next != protos_.end(); ++i, ++i_next)
            {
                gu_trace(gcomm::connect(*i, *i_next));
            }
            gu_trace(gcomm::connect(*i, this));
        }

        ~DummyNode()
        {
            try
            {
                std::list<Protolay*>::iterator i, i_next;
                i = i_next = protos_.begin();
                for (++i_next; i_next != protos_.end(); ++i, ++i_next)
                {
                    gu_trace(gcomm::disconnect(*i, *i_next));
                }
                gu_trace(gcomm::disconnect(*i, this));
                std::for_each(protos_.begin(), protos_.end(), gu::DeleteObject());
            }
            catch(std::exception& e)
            {
                log_fatal << e.what();
                abort();
            }
        }


        const UUID& uuid() const { return uuid_; }

        std::list<Protolay*>& protos() { return protos_; }

        size_t index() const { return index_; }

        void connect(bool first)
        {
            gu_trace(std::for_each(protos_.rbegin(), protos_.rend(),
                                   std::bind2nd(
                                       std::mem_fun(&Protolay::connect), first)));
        }

        void close(bool force = false)
        {
            for (std::list<Protolay*>::iterator i = protos_.begin();
                 i != protos_.end(); ++i)
            {
                (*i)->close();
            }
            // gu_trace(std::for_each(protos.rbegin(), protos.rend(),
            //                       std::mem_fun(&Protolay::close)));
        }


        void close(const UUID& uuid)
        {
            for (std::list<Protolay*>::iterator i = protos_.begin();
                 i != protos_.end(); ++i)
            {
                (*i)->close(uuid);
            }
            // gu_trace(std::for_each(protos.rbegin(), protos.rend(),
            //                       std::mem_fun(&Protolay::close)));
        }

        void send()
        {
            const int64_t seq(curr_seq_);
            gu::byte_t buf[sizeof(seq)];
            size_t sz;
            gu_trace(sz = gu::serialize8(seq, buf, sizeof(buf), 0));
            Datagram dg(gu::Buffer(buf, buf + sz));
            int err = send_down(dg, ProtoDownMeta(0));
            if (err != 0)
            {
                log_debug << "failed to send: " << strerror(err);
            }
            else
            {
                ++curr_seq_;
            }
        }

        Datagram create_datagram()
        {
            const int64_t seq(curr_seq_);
            gu::byte_t buf[sizeof(seq)];
            size_t sz;
            gu_trace(sz = gu::serialize8(seq, buf, sizeof(buf), 0));
            return Datagram (gu::Buffer(buf, buf + sz));
        }

        const Trace& trace() const { return tr_; }

        void set_cvi(const ViewId& vi)
        {
            log_debug << uuid() << " setting cvi to " << vi;
            cvi_ = vi;
        }

        bool in_cvi() const
        {
            for (Trace::ViewTraceMap::const_reverse_iterator i(
                     tr_.view_traces().rbegin());
                 i != tr_.view_traces().rend(); ++i)
            {
                if (i->first.uuid() == cvi_.uuid() &&
                    i->first.type() == cvi_.type() &&
                    i->first.seq()  >= cvi_.seq())
                {
                    return true;
                }
            }
            return false;
        }

        void handle_up(const void* cid, const Datagram& rb,
                       const ProtoUpMeta& um)
        {
            if (rb.len() != 0)
            {
                gcomm_assert((um.source() == UUID::nil()) == false);
                // assert(rb.header().size() == 0);
                const gu::byte_t* begin(gcomm::begin(rb));
                const size_t available(gcomm::available(rb));


                // log_debug << um.source() << " " << uuid()
                //         << " " << available ;
                // log_debug << rb.len() << " " << rb.offset() << " "
                //         << rb.header_len();
                if (available != 8)
                {
                    log_info << "check_trace fail: " << available;
                }
                gcomm_assert(available == 8);
                int64_t seq;
                gu_trace(gu::unserialize8(begin,
                                          available,
                                          0,
                                          seq));
                tr_.insert_msg(TraceMsg(um.source(), um.source_view_id(),
                                        seq));
            }
            else
            {
                gcomm_assert(um.has_view() == true);
                tr_.insert_view(um.view());
            }
        }


        gu::datetime::Date handle_timers()
        {
            std::for_each(protos_.begin(), protos_.end(),
                          std::mem_fun(&Protolay::handle_timers));
            return gu::datetime::Date::max();
        }

    private:
        size_t index_;
        UUID uuid_;
        std::list<Protolay*> protos_;
        ViewId cvi_;
        Trace tr_;
        int64_t curr_seq_;
    };



    class ChannelMsg
    {
    public:
        ChannelMsg(const Datagram& rb, const UUID& source) :
            rb_(rb),
            source_(source)
        {
        }
        const Datagram& rb() const { return rb_; }
        const UUID& source() const { return source_; }
    private:
        Datagram rb_;
        UUID source_;
    };


    class Channel : public Bottomlay
    {
    public:
        Channel(gu::Config& conf,
                const size_t ttl = 1,
                const size_t latency = 1,
                const double loss = 1.) :
            Bottomlay(conf),
            ttl_(ttl),
            latency_(latency),
            loss_(loss),
            queue_()
        { }



        ~Channel() { }

        int handle_down(Datagram& wb, const ProtoDownMeta& dm)
        {
            gcomm_assert((dm.source() == UUID::nil()) == false);
            gu_trace(put(wb, dm.source()));
            return 0;
        }

        void put(const Datagram& rb, const UUID& source);
        ChannelMsg get();
        void set_ttl(const size_t t) { ttl_ = t; }
        size_t ttl() const { return ttl_; }
        void set_latency(const size_t l)
        {
            gcomm_assert(l > 0);
            latency_ = l;
        }
        size_t latency() const { return latency_; }
        void set_loss(const double l) { loss_ = l; }
        double loss() const { return loss_; }
        size_t n_msgs() const
        {
            return queue_.size();
        }
    private:
        size_t ttl_;
        size_t latency_;
        double loss_;
        std::deque<std::pair<size_t, ChannelMsg> > queue_;
    };


    std::ostream& operator<<(std::ostream& os, const Channel& ch);
    std::ostream& operator<<(std::ostream& os, const Channel* ch);




    class MatrixElem
    {
    public:
        MatrixElem(const size_t ii, const size_t jj) : ii_(ii), jj_(jj) { }
        size_t ii() const { return ii_; }
        size_t jj() const { return jj_; }
        bool operator<(const MatrixElem& cmp) const
        {
            return (ii_ < cmp.ii_ || (ii_ == cmp.ii_ && jj_ < cmp.jj_));
        }
    private:
        size_t ii_;
        size_t jj_;
    };

    std::ostream& operator<<(std::ostream& os, const MatrixElem& me);

    class ChannelMap : public  Map<MatrixElem, Channel*>
    {
    public:
        struct DeleteObject
        {
            void operator()(ChannelMap::value_type& vt)
            {
                delete ChannelMap::value(vt);
            }
        };
    };
    class NodeMap : public Map<size_t, DummyNode*> {
    public:
        struct DeleteObject
        {
            void operator()(NodeMap::value_type& vt)
            {
                delete NodeMap::value(vt);
            }
        };

    };

    class PropagationMatrix
    {
    public:
        PropagationMatrix() : tp_(), prop_()
        {
            // Some tests which deal with timer expiration require that
            // the current time is far enough from zero. Start from
            // 100 secs after zero, this should give enough headroom
            // for all tests.
            gu::datetime::SimClock::init(100*gu::datetime::Sec);
            // Uncomment this to get logs with simulated timestamps.
            // The low will be written into stderr.
            // gu_log_cb = check_trace_log_cb;
        }
        ~PropagationMatrix();

        void insert_tp(DummyNode* t);
        void set_latency(const size_t ii, const size_t jj, const size_t lat);
        void set_loss(const size_t ii, const size_t jj, const double loss);
        void split(const size_t ii, const size_t jj);
        void merge(const size_t ii, const size_t jj, const double loss = 1.0);
        void propagate_n(size_t n);
        void propagate_until_empty();
        void propagate_until_cvi(bool handle_timers);
        friend std::ostream& operator<<(std::ostream&,const PropagationMatrix&);
    private:
        void expire_timers();


        size_t count_channel_msgs() const;
        bool all_in_cvi() const;

        NodeMap    tp_;
        ChannelMap prop_;
    };


    std::ostream& operator<<(std::ostream& os, const PropagationMatrix& prop);

    // Cross check traces from vector of dummy nodes
    void check_trace(const std::vector<DummyNode*>& nvec);

} // namespace gcomm
