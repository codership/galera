/*
 * Copyright (C) 2009-2019 Codership Oy <info@codership.com>
 */

/*!
 * @file protolay.hpp
 *
 * @brief Protocol layer interface definitions.
 *
 * Protocol layer interface allows construction of protocol stacks
 * with consistent interface to send messages upwards or downwards in
 * stack.
 */

#ifndef GCOMM_PROTOLAY_HPP
#define GCOMM_PROTOLAY_HPP

#include "gcomm/view.hpp"
#include "gcomm/exception.hpp"
#include "gcomm/order.hpp"
#include "gcomm/datagram.hpp"

#include "gu_logger.hpp"
#include "gu_datetime.hpp"
#include "gu_config.hpp"
#include "gu_status.hpp"

#include <cerrno>

#include <list>
#include <utility>

#include <boost/function.hpp>

// Declarations
namespace gcomm
{
    /*!
     * @class ProtoUpMeta
     *
     * Container for metadata passed upwards in protocol stack.
     */
    class ProtoUpMeta;
    std::ostream& operator<<(std::ostream&, const ProtoUpMeta&);

    /*!
     * @class ProtoDownMeta
     *
     * Container for metadata passed downwards in protocol stack.
     */
    class ProtoDownMeta;

    /*!
     * @class Protolay
     *
     * Protocol layer interface.
     */
    class Protolay;

    /*!
     * @class Toplay
     *
     * Protolay that is on the top of the protocol stack.
     */
    class Toplay;

    /*!
     * @class Bottomlay
     *
     * Protolay that is on the bottom of the protocol stack.
     */
    class Bottomlay;

    void connect(Protolay*, Protolay*);
    void disconnect(Protolay*, Protolay*);
}

/* message context to pass up with the data buffer? */
class gcomm::ProtoUpMeta
{
public:
    ProtoUpMeta(const int err_no) :
        source_(),
        source_view_id_(),
        user_type_(),
        order_(),
        to_seq_(),
        err_no_(err_no),
        view_(0)
    { }

    ProtoUpMeta(const UUID    source         = UUID::nil(),
                const ViewId  source_view_id = ViewId(),
                const View*   view           = 0,
                const uint8_t user_type      = 0xff,
                const Order   order          = O_DROP,
                const int64_t to_seq         = -1,
                const int err_no = 0) :
        source_         (source         ),
        source_view_id_ (source_view_id ),
        user_type_      (user_type      ),
        order_          (order          ),
        to_seq_         (to_seq         ),
        err_no_         (err_no         ),
        view_           (view != 0 ? new View(*view) : 0)
    { }

    ProtoUpMeta(const ProtoUpMeta& um) :
        source_         (um.source_         ),
        source_view_id_ (um.source_view_id_ ),
        user_type_      (um.user_type_      ),
        order_          (um.order_          ),
        to_seq_         (um.to_seq_         ),
        err_no_         (um.err_no_         ),
        view_           (um.view_ ? new View(*um.view_) : 0)
    { }

    ~ProtoUpMeta() { delete view_; }

    const UUID&   source()         const { return source_; }

    const ViewId& source_view_id() const { return source_view_id_; }

    uint8_t       user_type()      const { return user_type_; }

    Order         order()          const { return order_; }

    int64_t       to_seq()         const { return to_seq_; }

    int           err_no()          const { return err_no_; }

    bool          has_view()           const { return view_ != 0; }

    const View&   view()           const { return *view_; }

private:
    ProtoUpMeta& operator=(const ProtoUpMeta&);

    UUID    const source_;
    ViewId  const source_view_id_;
    uint8_t const user_type_;
    Order   const order_;
    int64_t const to_seq_;
    int     const err_no_;
    View*   const view_;
};

inline std::ostream& gcomm::operator<<(std::ostream& os, const ProtoUpMeta& um)
{
    os << "proto_up_meta: { ";
    if (not (um.source() == UUID::nil()))
    {
        os << "source=" << um.source() << ",";
    }
    if (um.source_view_id().type() != V_NONE)
    {
        os << "source_view_id=" << um.source_view_id() << ",";
    }
    os << "user_type=" << static_cast<int>(um.user_type()) << ",";
    os << "to_seq=" << um.to_seq() << ",";
    if (um.has_view() == true)
    {
        os << "view=" << um.view();
    }
    os << "}";
    return os;
}

/* message context to pass down? */
class gcomm::ProtoDownMeta
{
public:
    ProtoDownMeta(const uint8_t user_type = 0xff,
                  const Order   order     = O_SAFE,
                  const UUID&   source      = UUID::nil(),
                  const int     segment   = 0) :
        user_type_ (user_type),
        order_     (order),
        source_    (source),
        target_    (UUID::nil()),
        segment_   (segment)
    { }

    ProtoDownMeta(const UUID& target)
        : user_type_(0xff)
        , order_ (O_SAFE)
        , source_(UUID::nil())
        , target_(target)
        , segment_(0)
    { }

    uint8_t     user_type() const { return user_type_; }
    Order       order()     const { return order_;     }
    const UUID& source()    const { return source_;    }
    const UUID& target()    const { return target_;    }
    int         segment()   const { return segment_;   }
private:
    const uint8_t user_type_;
    const Order   order_;
    const UUID    source_;
    const UUID    target_;
    const int     segment_;
};

class gcomm::Protolay
{
public:
    typedef Map<UUID, gu::datetime::Date> EvictList;

    typedef boost::function<void()> sync_param_cb_t;

    virtual ~Protolay() {}

    virtual void connect(bool) { }
    virtual void close(bool force = false) { }
    virtual void close(const UUID& uuid) { }

    /* apparently handles data from upper layer. what is return value? */
    virtual int  handle_down (Datagram&, const ProtoDownMeta&) = 0;
    virtual void handle_up   (const void*, const Datagram&, const ProtoUpMeta&) = 0;

    void set_up_context(Protolay *up)
    {
        if (std::find(up_context_.begin(),
                      up_context_.end(),
                      up) != up_context_.end())
        {
            gu_throw_fatal << "up context already exists";
        }
        up_context_.push_back(up);
    }

    void set_down_context(Protolay *down)
    {
        if (std::find(down_context_.begin(),
                      down_context_.end(),
                      down) != down_context_.end())
        {
            gu_throw_fatal << "down context already exists";
        }
        down_context_.push_back(down);
    }

    void unset_up_context(Protolay* up)
    {
        CtxList::iterator i;
        if ((i = std::find(up_context_.begin(),
                           up_context_.end(),
                           up)) == up_context_.end())
        {
            gu_throw_fatal << "up context does not exist";
        }
        up_context_.erase(i);
    }


    void unset_down_context(Protolay* down)
    {
        CtxList::iterator i;
        if ((i = std::find(down_context_.begin(),
                           down_context_.end(),
                           down)) == down_context_.end())
        {
            gu_throw_fatal << "down context does not exist";
        }
        down_context_.erase(i);
    }

    /* apparently passed data buffer to the upper layer */
    void send_up(const Datagram& dg, const ProtoUpMeta& up_meta)
    {
        if (up_context_.empty() == true)
        {
            gu_throw_fatal << this << " up context(s) not set";
        }

        CtxList::iterator i, i_next;
        for (i = up_context_.begin(); i != up_context_.end(); i = i_next)
        {
            i_next = i, ++i_next;
            (*i)->handle_up(this, dg, up_meta);
        }
    }

    /* apparently passes data buffer to lower layer, what is return value? */
    int send_down(Datagram& dg, const ProtoDownMeta& down_meta)
    {
        if (down_context_.empty() == true)
        {
            log_warn << this << " down context(s) not set";
            return ENOTCONN;
        }

        int    ret         = 0;
        for (CtxList::iterator i = down_context_.begin();
             i != down_context_.end(); ++i)
        {
            const size_t hdr_offset(dg.header_offset());
            int err = (*i)->handle_down(dg, down_meta);
            // Verify that lower layer rolls back any modifications to
            // header
            if (hdr_offset != dg.header_offset())
            {
                gu_throw_fatal;
            }
            if (err != 0)
            {
                ret = err;
            }
        }
        return ret;
    }

    virtual void handle_stable_view(const View& view) { }

    void set_stable_view(const View& view)
    {
        for (CtxList::iterator i(down_context_.begin());
             i != down_context_.end(); ++i)
        {
            (*i)->handle_stable_view(view);
        }
    }


    virtual void handle_evict(const UUID& uuid) { }

    void evict(const UUID& uuid)
    {
        evict_list_.insert(
            std::make_pair(uuid, gu::datetime::Date::monotonic()));
        handle_evict(uuid);
        for (CtxList::iterator i(down_context_.begin());
             i != down_context_.end(); ++i)
        {
            (*i)->evict(uuid);
        }
    }

    void unevict(const UUID& uuid)
    {
        evict_list_.erase(uuid);
        for (CtxList::iterator i(down_context_.begin());
             i != down_context_.end(); ++i)
        {
            (*i)->unevict(uuid);
        }
    }

    bool is_evicted(const UUID& uuid) const
    {
        if (down_context_.empty())
        {
            return (evict_list_.find(uuid) != evict_list_.end());
        }
        else
        {
            return (*down_context_.begin())->is_evicted(uuid);
        }
    }

    const EvictList& evict_list() const { return evict_list_; }

    virtual void handle_get_status(gu::Status& status) const
    { }

    void get_status(gu::Status& status) const
    {
        for (CtxList::const_iterator i(down_context_.begin());
             i != down_context_.end(); ++i)
        {
            (*i)->get_status(status);
        }
        handle_get_status(status);
    }


    std::string get_address(const UUID& uuid) const
    {
        if (down_context_.empty()) return handle_get_address(uuid);
        else return (*down_context_.begin())->get_address(uuid);
    }

    virtual std::string handle_get_address(const UUID& uuid) const
    {
        return "(unknown)";
    }


    virtual gu::datetime::Date handle_timers()
    {
        return gu::datetime::Date::max();
    }

    virtual bool set_param(const std::string& key, const std::string& val, 
                           sync_param_cb_t& sync_param_cb)
    {
        return false;
    }

    const Protolay* id() const { return this; }

protected:
    Protolay(gu::Config& conf)
        :
        conf_(conf),
        up_context_(0),
        down_context_(0),
        evict_list_()
    { }

    gu::Config& conf_;

private:
    typedef std::list<Protolay*> CtxList;
    CtxList     up_context_;
    CtxList     down_context_;

    EvictList   evict_list_;


    Protolay (const Protolay&);
    Protolay& operator=(const Protolay&);
};


class gcomm::Toplay : protected Conf::Check, public Protolay
{
public:
    Toplay(gu::Config& conf) : Conf::Check(conf), Protolay(conf) { }
private:
    int handle_down(Datagram& dg, const ProtoDownMeta& dm)
    {
        gu_throw_fatal << "Toplay handle_down() called";
    }
};

class gcomm::Bottomlay : public Protolay
{
public:
    Bottomlay(gu::Config& conf) : Protolay(conf) { }
private:
    void handle_up(const void* id, const Datagram&, const ProtoUpMeta& um)
    {
        gu_throw_fatal << "Bottomlay handle_up() called";
    }
};

inline void gcomm::connect(Protolay* down, Protolay* up)
{
    down->set_up_context(up);
    up->set_down_context(down);
}

inline void gcomm::disconnect(Protolay* down, Protolay* up)
{
    down->unset_up_context(up);
    up->unset_down_context(down);
}


#endif /* GCOMM_PROTOLAY_HPP */
