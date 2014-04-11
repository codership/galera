/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
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

#include <cerrno>

#include <list>
#include <utility>


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
                  const UUID&   uuid      = UUID::nil(),
                  const int     segment   = 0) :
        user_type_ (user_type),
        order_     (order),
        source_    (uuid),
        segment_   (segment)
    { }

    uint8_t     user_type() const { return user_type_; }
    Order       order()     const { return order_;     }
    const UUID& source()    const { return source_;    }
    int         segment()   const { return segment_;   }
private:
    const uint8_t user_type_;
    const Order   order_;
    const UUID    source_;
    const int     segment_;
};

class gcomm::Protolay
{
    typedef std::list<Protolay*> CtxList;
    CtxList     up_context_;
    CtxList     down_context_;

    Protolay (const Protolay&);
    Protolay& operator=(const Protolay&);

protected:

    gu::Config& conf_;

    Protolay(gu::Config& conf)
        :
        up_context_(0),
        down_context_(0),
        conf_(conf)
    { }

public:

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

    virtual gu::datetime::Date handle_timers()
    {
        return gu::datetime::Date::max();
    }

    virtual bool set_param(const std::string& key, const std::string& val)
    {
        return false;
    }

    const Protolay* id() const { return this; }

};

class gcomm::Toplay : public Protolay
{
public:
    Toplay(gu::Config& conf) : Protolay(conf) { }
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
