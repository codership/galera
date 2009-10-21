/*!
 * @file Base class for all gcomm layers
 */

#ifndef _GCOMM_PROTOLAY_HPP_
#define _GCOMM_PROTOLAY_HPP_

#include <cerrno>

#include <gcomm/common.hpp>
#include <gcomm/util.hpp>
#include <gcomm/readbuf.hpp>
#include <gcomm/writebuf.hpp>
#include <gcomm/view.hpp>
#include <gcomm/exception.hpp>
#include <gcomm/logger.hpp>
#include <gcomm/safety_prefix.hpp>
#include "gcomm/time.hpp"

#include <list>
#include <utility>

namespace gcomm
{
    class ProtoUpMeta;
    std::ostream& operator<<(std::ostream&, const ProtoUpMeta&);
    class ProtoDownMeta;
    class Protolay;
    class Toplay;
    class Bottomlay;

    void connect(Protolay*, Protolay*, int);
    void disconnect(Protolay*, Protolay*, int);
}

/* message context to pass up with the data buffer? */
class gcomm::ProtoUpMeta
{
public:
    ProtoUpMeta(const UUID    source_         = UUID::nil(),
                const ViewId  source_view_id_ = ViewId(),
                const View*   view_           = 0,
                const uint8_t user_type_      = 0xff,
                const int64_t to_seq_         = -1) :
        source         (source_      ),
        source_view_id (source_view_id_ ),
        user_type      (user_type_   ),
        to_seq         (to_seq_      ),
        view           (view_ != 0 ? new View(*view_) : 0)
    { }

    ProtoUpMeta(const ProtoUpMeta& um) :
        source         (um.source      ),
        source_view_id (um.source_view_id ),
        user_type      (um.user_type   ),
        to_seq         (um.to_seq      ),
        view           (um.view ? new View(*um.view) : 0)
    { }

    ~ProtoUpMeta() { delete view; }
    
    const UUID&   get_source()         const { return source; }
    
    const ViewId& get_source_view_id() const { return source_view_id; }

    uint8_t       get_user_type()      const { return user_type; }
    
    int64_t       get_to_seq()         const { return to_seq; }
    
    bool          has_view()           const { return view != 0; }
    
    const View&   get_view()           const { return *view; }

private:
    ProtoUpMeta& operator=(const ProtoUpMeta&);
    
    UUID    const source;
    ViewId  const source_view_id;
    uint8_t const user_type;
    int64_t const to_seq;
    View*   const view; // @todo: this makes default constructor pointless
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
    const uint8_t      user_type;
    const SafetyPrefix sp;
    const UUID         source;
public:
    
    ProtoDownMeta(const uint8_t user_type_ = 0xff, 
                  const SafetyPrefix sp_   = SP_SAFE,
                  const UUID& uuid_        = UUID::nil()) : 
        user_type(user_type_), 
        sp(sp_),
        source(uuid_)
    { }
    
    uint8_t get_user_type() const { return user_type; }
    
    SafetyPrefix get_safety_prefix() const { return sp; }

    const UUID& get_source() const { return source; }
};

class gcomm::Protolay
{
    typedef std::list<std::pair<Protolay*, int> > CtxList;
    CtxList up_context;
    CtxList down_context;
    
    Protolay (const Protolay&);
    Protolay& operator=(const Protolay&);
    
protected:
    
    Protolay() : 
        up_context(0), 
        down_context(0)
    {}
    
public:
    
    virtual ~Protolay() {}

    virtual void connect(bool) { }
    virtual void close() { }
    
    /* apparently handles data from upper layer. what is return value? */
    virtual int  handle_down (WriteBuf *, const ProtoDownMeta&) = 0;
    
    virtual void handle_up   (int cid, const ReadBuf *,
                              size_t, const ProtoUpMeta&) = 0;
    
    
    void set_up_context(Protolay *up, int id = -1)
    {
	if (std::find(up_context.begin(), up_context.end(),
                      std::make_pair(up, id)) != up_context.end())
        {
            gcomm_throw_fatal << "up context already exists";
        }
	up_context.push_back(std::make_pair(up, id));
    }
    
    void set_down_context(Protolay *down, int id = -1)
    {
	if (std::find(down_context.begin(), 
                      down_context.end(),
                      std::make_pair(down, id)) != down_context.end())
        {
            gcomm_throw_fatal << "down context already exists";
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
            gcomm_throw_fatal << "up context does not exist";
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
            gcomm_throw_fatal << "down context does not exist";
        }
        down_context.erase(i);
    }

    void release()
    {
        for (CtxList::iterator i = up_context.begin(); i !=  up_context.end();
             ++i)
        {
            i->first->unset_down_context(this, i->second);
            this->unset_up_context(i->first, i->second);
        }
        for (CtxList::iterator i = down_context.begin();
             i != down_context.end(); ++i)
        {
            i->first->unset_up_context(this, i->second);
            this->unset_down_context(i->first, i->second);
        }
    }


    
    /* apparently passed data buffer to the upper layer */
    void pass_up(const ReadBuf *rb, 
                 const size_t offset, 
                 const ProtoUpMeta& up_meta)
    {
	if (up_context.empty() == true)
        {
	    gcomm_throw_fatal << this << " up context(s) not set";
	}
        
        CtxList::iterator i, i_next;
        for (i = up_context.begin(); i != up_context.end(); i = i_next)
        {
            i_next = i, ++i_next;
            i->first->handle_up(i->second, rb, offset, up_meta);
        }

    }
    
        
    /* apparently passes data buffer to lower layer, what is return value? */
    int pass_down(WriteBuf *wb, const ProtoDownMeta& down_meta)
    {
	if (down_context.empty() == true)
        {
            log_warn << this << " down context(s) not set";
            return 0;
	}
        
	// Simple guard of wb consistency in form of testing 
	// writebuffer header length. 
	size_t down_hdrlen = wb->get_hdrlen();
	int    ret         = 0;
        

        for (CtxList::iterator i = down_context.begin(); 
             i != down_context.end(); ++i)
        {
            int err = i->first->handle_down(wb, down_meta);
            if (err != 0)
            {
                ret = err;
            }
        }
        
	if (down_hdrlen != wb->get_hdrlen())
        {
            gcomm_throw_fatal << "hdr not rolled back";
        }
        
	return ret;
    }    


    virtual Time handle_timers() { return Time::max(); }
};

class gcomm::Toplay : public Protolay
{
    int handle_down(WriteBuf *wb, const ProtoDownMeta& dm)
    {
	gcomm_throw_fatal << "Toplay handle_down() called";
	throw;
    }
};

class gcomm::Bottomlay : public Protolay
{
    void handle_up(int cid, const ReadBuf *rb, size_t offset, 
                   const ProtoUpMeta& um)
    {
	gcomm_throw_fatal << "Bottomlay handle_up() called";
    }
};

inline void gcomm::connect(Protolay* down, Protolay* up, int cid = -1)
{
    down->set_up_context(up, cid);
    up->set_down_context(down, cid);
}

inline void gcomm::disconnect(Protolay* down, Protolay* up, int cid = -1)
{
    down->unset_up_context(up, cid);
    up->unset_down_context(down, cid);
}


#endif /* _GCOMM_PROTOLAY_HPP_ */
