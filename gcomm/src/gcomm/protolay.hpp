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

#include <cerrno>

#include <list>
#include <utility>


// Forward declarations
namespace gu
{
    namespace net
    {
        class Datagram;
    }
}


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
    
    void connect(Protolay*, Protolay*, int);
    void disconnect(Protolay*, Protolay*, int);
}

/* message context to pass up with the data buffer? */
class gcomm::ProtoUpMeta
{
public:
    ProtoUpMeta(const int err_no_) :
        source(),
        source_view_id(),
        user_type(),
        to_seq(),
        err_no(err_no_),
        view(0)
    { }
    
    ProtoUpMeta(const UUID    source_         = UUID::nil(),
                const ViewId  source_view_id_ = ViewId(),
                const View*   view_           = 0,
                const uint8_t user_type_      = 0xff,
                const int64_t to_seq_         = -1,
                const int err_no_ = 0) :
        source         (source_      ),
        source_view_id (source_view_id_ ),
        user_type      (user_type_   ),
        to_seq         (to_seq_      ),
        err_no         (err_no_),
        view           (view_ != 0 ? new View(*view_) : 0)
    { }

    ProtoUpMeta(const ProtoUpMeta& um) :
        source         (um.source      ),
        source_view_id (um.source_view_id ),
        user_type      (um.user_type   ),
        to_seq         (um.to_seq      ),
        err_no         (um.err_no),
        view           (um.view ? new View(*um.view) : 0)
    { }

    ~ProtoUpMeta() { delete view; }
    
    const UUID&   get_source()         const { return source; }
    
    const ViewId& get_source_view_id() const { return source_view_id; }

    uint8_t       get_user_type()      const { return user_type; }
    
    int64_t       get_to_seq()         const { return to_seq; }

    int           get_errno()          const { return err_no; }
    
    bool          has_view()           const { return view != 0; }
    
    const View&   get_view()           const { return *view; }

private:
    ProtoUpMeta& operator=(const ProtoUpMeta&);
    
    UUID    const source;
    ViewId  const source_view_id;
    uint8_t const user_type;
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
    typedef std::list<std::pair<Protolay*, int> > CtxList;
    CtxList up_context;
    CtxList down_context;
    
    Protolay (const Protolay&);
    Protolay& operator=(const Protolay&);
    
protected:
    int id;
    Protolay() : 
        up_context(0), 
        down_context(0),
        id(-1)
    {}
    
public:
    
    virtual ~Protolay() {}
    
    virtual void connect(bool) { }
    virtual void close() { }
    virtual void close(const UUID& uuid) { }
    
    /* apparently handles data from upper layer. what is return value? */
    virtual int  handle_down (const gu::net::Datagram&, const ProtoDownMeta&) = 0;
    virtual void handle_up   (int, const gu::net::Datagram&, const ProtoUpMeta&) = 0;
    
    void set_id(const int id_) { id = id_; }

    void set_up_context(Protolay *up, int id = -1)
    {
	if (std::find(up_context.begin(), up_context.end(),
                      std::make_pair(up, id)) != up_context.end())
        {
            gu_throw_fatal << "up context already exists";
        }
	up_context.push_back(std::make_pair(up, id));
    }
    
    void set_down_context(Protolay *down, int id = -1)
    {
	if (std::find(down_context.begin(), 
                      down_context.end(),
                      std::make_pair(down, id)) != down_context.end())
        {
            gu_throw_fatal << "down context already exists";
        }
	down_context.push_back(std::make_pair(down, id));
    }
    
    void unset_up_context(Protolay* up, int id = -1)
    {
        std::list<std::pair<Protolay*, int> >::iterator i;
	if ((i = std::find(up_context.begin(), 
                           up_context.end(),
                           std::make_pair(up, id))) == up_context.end())
        { 
            gu_throw_fatal << "up context does not exist";
        }
        up_context.erase(i);
    }
    
    
    void unset_down_context(Protolay* down, int id = -1)
    {
        std::list<std::pair<Protolay*, int> >::iterator i;
	if ((i = std::find(down_context.begin(), 
                           down_context.end(),
                           std::make_pair(down, id))) == down_context.end()) 
        {
            gu_throw_fatal << "down context does not exist";
        }
        down_context.erase(i);
    }
    
    /* apparently passed data buffer to the upper layer */
    void send_up(const gu::net::Datagram& dg, const ProtoUpMeta& up_meta)
    {
	if (up_context.empty() == true)
        {
	    gu_throw_fatal << this << " up context(s) not set";
	}
        
        CtxList::iterator i, i_next;
        for (i = up_context.begin(); i != up_context.end(); i = i_next)
        {
            i_next = i, ++i_next;
            i->first->handle_up(i->second, dg, up_meta);
        }
    }
    
    /* apparently passes data buffer to lower layer, what is return value? */
    int send_down(const gu::net::Datagram& dg, const ProtoDownMeta& down_meta)
    {
	if (down_context.empty() == true)
        {
            log_warn << this << " down context(s) not set";
            return 0;
	}
        
	int    ret         = 0;
        for (CtxList::iterator i = down_context.begin(); 
             i != down_context.end(); ++i)
        {
            int err = i->first->handle_down(dg, down_meta);
            if (err != 0)
            {
                ret = err;
            }
        }
	return ret;
    }    
    
    virtual gu::datetime::Date handle_timers() { return gu::datetime::Date::max(); }

    int get_id() const { return id; }

    virtual void handle_stable_view(const View& view) { }

    void set_stable_view(const View& view)
    {
        for (CtxList::iterator i(down_context.begin());
             i != down_context.end(); ++i)
        {
            i->first->handle_stable_view(view);
        }
    }
};

class gcomm::Toplay : public Protolay
{
    int handle_down(const gu::net::Datagram& dg, const ProtoDownMeta& dm)
    {
	gu_throw_fatal << "Toplay handle_down() called";
	throw;
    }
};

class gcomm::Bottomlay : public Protolay
{
    void handle_up(int id, const gu::net::Datagram&, const ProtoUpMeta& um)
    {
	gu_throw_fatal << "Bottomlay handle_up() called";
    }
};

inline void gcomm::connect(Protolay* down, Protolay* up, int id = -1)
{
    down->set_up_context(up, id);
    up->set_down_context(down, id);
}

inline void gcomm::disconnect(Protolay* down, Protolay* up, int id = -1)
{
    down->unset_up_context(up, id);
    up->unset_down_context(down, id);
}


#endif /* GCOMM_PROTOLAY_HPP */
