/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
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

#include "gu_logger.hpp"
#include "gu_datetime.hpp"
#include "gu_datagram.hpp"
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
    ProtoUpMeta(const int err_no_) :
        source(),
        source_view_id(),
        user_type(),
        order(),
        to_seq(),
        err_no(err_no_),
        view(0)
    { }

    ProtoUpMeta(const UUID    source_         = UUID::nil(),
                const ViewId  source_view_id_ = ViewId(),
                const View*   view_           = 0,
                const uint8_t user_type_      = 0xff,
                const Order   order_          = O_DROP,
                const int64_t to_seq_         = -1,
                const int err_no_ = 0) :
        source         (source_      ),
        source_view_id (source_view_id_ ),
        user_type      (user_type_   ),
        order          (order_),
        to_seq         (to_seq_      ),
        err_no         (err_no_),
        view           (view_ != 0 ? new View(*view_) : 0)
    { }

    ProtoUpMeta(const ProtoUpMeta& um) :
        source         (um.source      ),
        source_view_id (um.source_view_id ),
        user_type      (um.user_type   ),
        order          (um.order       ),
        to_seq         (um.to_seq      ),
        err_no         (um.err_no),
        view           (um.view ? new View(*um.view) : 0)
    { }

    ~ProtoUpMeta() { delete view; }

    const UUID&   get_source()         const { return source; }

    const ViewId& get_source_view_id() const { return source_view_id; }

    uint8_t       get_user_type()      const { return user_type; }

    Order         get_order()          const { return order; }

    int64_t       get_to_seq()         const { return to_seq; }

    int           get_errno()          const { return err_no; }

    bool          has_view()           const { return view != 0; }

    const View&   get_view()           const { return *view; }

private:
    ProtoUpMeta& operator=(const ProtoUpMeta&);

    UUID    const source;
    ViewId  const source_view_id;
    uint8_t const user_type;
    Order   const order;
    int64_t const to_seq;
    int     const err_no;
    View*   const view;
};

inline std::ostream& gcomm::operator<<(std::ostream& os, const ProtoUpMeta& um)
{
    os << "proto_up_meta: { ";
    if (not (um.get_source() == UUID::nil()))
    {
        os << "source=" << um.get_source() << ",";
    }
    if (um.get_source_view_id().get_type() != V_NONE)
    {
        os << "source_view_id=" << um.get_source_view_id() << ",";
    }
    os << "user_type=" << static_cast<int>(um.get_user_type()) << ",";
    os << "to_seq=" << um.get_to_seq() << ",";
    if (um.has_view() == true)
    {
        os << "view=" << um.get_view();
    }
    os << "}";
    return os;
}

/* message context to pass down? */
class gcomm::ProtoDownMeta
{
public:
    ProtoDownMeta(const uint8_t user_type_ = 0xff,
                  const Order   order_     = O_SAFE,
                  const UUID&   uuid_      = UUID::nil()) :
        user_type (user_type_),
        order     (order_),
        source    (uuid_)
    { }

    uint8_t     get_user_type() const { return user_type; }
    Order       get_order()     const { return order;     }
    const UUID& get_source()    const { return source;    }
private:
    const uint8_t user_type;
    const Order   order;
    const UUID    source;
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
    virtual void close() { }
    virtual void close(const UUID& uuid) { }

    /* apparently handles data from upper layer. what is return value? */
    virtual int  handle_down (gu::Datagram&, const ProtoDownMeta&) = 0;
    virtual void handle_up   (const void*, const gu::Datagram&, const ProtoUpMeta&) = 0;

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
    void send_up(const gu::Datagram& dg, const ProtoUpMeta& up_meta)
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
    int send_down(gu::Datagram& dg, const ProtoDownMeta& down_meta)
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
            const size_t hdr_offset(dg.get_header_offset());
            int err = (*i)->handle_down(dg, down_meta);
            // Verify that lower layer rolls back any modifications to
            // header
            if (hdr_offset != dg.get_header_offset())
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

    const Protolay* get_id() const { return this; }

};

class gcomm::Toplay : public Protolay
{
public:
    Toplay(gu::Config& conf) : Protolay(conf) { }
private:
    int handle_down(gu::Datagram& dg, const ProtoDownMeta& dm)
    {
	gu_throw_fatal << "Toplay handle_down() called";
	throw;
    }
};

class gcomm::Bottomlay : public Protolay
{
public:
    Bottomlay(gu::Config& conf) : Protolay(conf) { }
private:
    void handle_up(const void* id, const gu::Datagram&, const ProtoUpMeta& um)
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
