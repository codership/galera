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



namespace gcomm
{
    class ProtoUpMeta;
    class ProtoDownMeta;
    class Protolay;
    class Toplay;
    class Bottomlay;

    void connect(Protolay*, Protolay*);
    void disconnect(Protolay*, Protolay*);
}

/* message context to pass up with the data buffer? */
class gcomm::ProtoUpMeta
{
    UUID    const source;
    uint8_t const user_type;
    int64_t const to_seq;
    View*   const view; // @todo: this makes default constructor pointless
    
    ProtoUpMeta& operator=(const ProtoUpMeta&);

public:

    ProtoUpMeta(const UUID source_ = UUID::nil(),
                const View* view_  = 0,
                const uint8_t user_type_ = 0xff,
                const int64_t to_seq_ = -1) :
        source(source_),
        user_type(user_type_),
        to_seq(to_seq_),
        view(view_ != 0 ? new View(*view_) : 0)
    {

    }

    ProtoUpMeta(const ProtoUpMeta& um) :
        source    (um.source),
        user_type (um.user_type),
        to_seq    (um.to_seq),
        view      (um.view ? new View(*um.view) : 0)
    {}

    ~ProtoUpMeta() { delete view; }
    
    const UUID& get_source()    const { return source; }
    
    uint8_t     get_user_type() const { return user_type; }
    
    int64_t     get_to_seq()    const { return to_seq; }
    
    bool        has_view()      const { return view != 0; }
    
    const View& get_view()      const { return *view; }
};

/* message context to pass down? */
class gcomm::ProtoDownMeta
{
    const uint8_t      user_type;
    const SafetyPrefix sp;
public:
    
    ProtoDownMeta(const uint8_t user_type_ = 0xff, 
                  const SafetyPrefix sp_   = SP_SAFE) : 
        user_type(user_type_), 
        sp(sp_) 
    { }
    
    uint8_t get_user_type() const { return user_type; }
    
    SafetyPrefix get_safety_prefix() const { return sp; }
};

class gcomm::Protolay
{
    int       context_id; // why there are two contexts but only one id?
    Protolay *up_context;
    Protolay *down_context;
    bool      released;

    Protolay (const Protolay&);
    Protolay& operator=(const Protolay&);

protected:

    Protolay() : context_id(-1), up_context(0), down_context(0), released(false)
    {}

public:

    void release()
    {
        up_context   = 0;
        down_context = 0;
        released     = true;
    }
    
    virtual ~Protolay() {}

    /* apparently handles data from upper layer. what is return value? */
    virtual int  handle_down (WriteBuf *, const ProtoDownMeta&) = 0;

    virtual void handle_up   (int cid, const ReadBuf *,
                              size_t, const ProtoUpMeta&) = 0;

    
    void set_up_context(Protolay *up)
    {
	if (up_context) gcomm_throw_fatal << "Context already exists";

	up_context = up;
    }
    
    void set_up_context(Protolay *up, int id)
    {
	if (up_context) gcomm_throw_fatal << "Up context already exists";
        
	context_id = id;
	up_context = up;
    }

    void change_up_context(Protolay* old_up, Protolay* new_up)
    {
        if (up_context != old_up) 
        {
            gcomm_throw_fatal << "Context mismatch: " 
                              << old_up 
                              << " " 
                              << up_context;
        }
        
        up_context = new_up;
    }

    void change_down_context(Protolay* old_down, Protolay* new_down)
    {
        if (down_context != old_down) 
        {
            gcomm_throw_fatal << "Context mismatch: " 
                              << old_down 
                              << " "
                              << down_context;
        }
        
        down_context = new_down;
    }

    void set_down_context(Protolay *down, int id)
    {
	context_id   = id;
	down_context = down;
    }
    
    void set_down_context(Protolay *down)
    {
	down_context = down;
    }
    
    /* apparently passed data buffer to the upper layer */
    void pass_up(const ReadBuf *rb, size_t offset, const ProtoUpMeta& up_meta)
    {
	if (!up_context) {
	    gcomm_throw_fatal << "Up context not defined, released = " 
                              << released;
	}
        
	up_context->handle_up(context_id, rb, offset, up_meta);
    }
    
    /* apparently passes data buffer to lower layer, what is return value? */
    int pass_down(WriteBuf *wb, const ProtoDownMeta& down_meta)
    {
	if (!down_context) {
	    gcomm_throw_fatal << "Down context not defined, released = "
                              << released;
	}
        
	// Simple guard of wb consistency in form of testing 
	// writebuffer header length. 
	size_t down_hdrlen = wb->get_hdrlen();
	int    ret         = down_context->handle_down(wb, down_meta);

	if (down_hdrlen != wb->get_hdrlen())
        {
            gcomm_throw_fatal << "hdr not rolled back";
        }
        
	return ret;
    }    
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

inline void gcomm::connect(Protolay* down, Protolay* up)
{
    down->set_up_context(up);
    up->set_down_context(down);
}

inline void gcomm::disconnect(Protolay* down, Protolay* up)
{
    down->change_up_context(up, 0);
    up->change_down_context(down, 0);
}


#endif /* _GCOMM_PROTOLAY_HPP_ */
