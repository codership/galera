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




BEGIN_GCOMM_NAMESPACE

/* message context to pass up with the data buffer? */
class ProtoUpMeta
{
    UUID    const source;
    uint8_t const user_type;
    int64_t const to_seq;
    View*   const view; // @todo: this makes default constructor pointless
    
    ProtoUpMeta& operator=(const ProtoUpMeta&);

public:
#if 0
    ProtoUpMeta() :
        source   (),
        user_type(0xff),
        to_seq   (-1),
        view     (0)
    {}
#endif

    ProtoUpMeta(const ProtoUpMeta& um) :
        source    (um.source),
        user_type (um.user_type),
        to_seq    (um.to_seq),
        view      (um.view ? new View(*um.view) : 0)
    {}

    ProtoUpMeta(const View* view_, const int64_t to_seq_ = -1) :
        source    (),
        user_type (0xff),
        to_seq    (to_seq_),
        view      (new View(*view_))
    {}

    ProtoUpMeta(const UUID& source_, const uint8_t user_type_ = 0xff,
                const int64_t to_seq_ = -1) :
        source(source_),
        user_type(user_type_),
        to_seq(to_seq_),
        view(0)
    {}

    ~ProtoUpMeta() { delete view; }

    const UUID& get_source()    const { return source; }

    uint8_t     get_user_type() const { return user_type; }

    int64_t     get_to_seq()    const { return to_seq; }

    const View* get_view()      const { return view; }
};

/* message context to pass down? */
class ProtoDownMeta
{
    const uint8_t user_type;

public:

    ProtoDownMeta(const uint8_t user_type_) : user_type(user_type_) {}

    uint8_t get_user_type() const { return user_type; }
};

class Protolay
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
    virtual int  handle_down (WriteBuf *, const ProtoDownMeta*) = 0;

    virtual void handle_up   (const int cid, const ReadBuf *,
                              const size_t, const ProtoUpMeta*) = 0;

    
    void set_up_context(Protolay *up)
    {
	if (up_context) gcomm_throw_fatal << "Context already exists";

	up_context = up;
    }
    
    void set_up_context(Protolay *up, const int id)
    {
	if (up_context) gcomm_throw_fatal << "Up context already exists";

	context_id = id;
	up_context = up;
    }

    void change_up_context(Protolay* old_up, Protolay* new_up)
    {
        if (up_context != old_up) {
            gcomm_throw_fatal << "Context mismatch: " 
                              << old_up 
                              << " " 
                              << up_context;
        }
        
        up_context = new_up;
    }

    void change_down_context(Protolay* old_down, Protolay* new_down)
    {
        if (down_context != old_down) {
            gcomm_throw_fatal << "Context mismatch: " 
                              << old_down 
                              << " "
                              << down_context;
        }

        down_context = new_down;
    }

    void set_down_context(Protolay *down, const int id)
    {
	context_id   = id;
	down_context = down;
    }

    void set_down_context(Protolay *down)
    {
	down_context = down;
    }

    /* apparently passed data buffer to the upper layer */
    void pass_up(const ReadBuf *rb, const size_t roff,
                 const ProtoUpMeta *up_meta)
    {
	if (!up_context) {
	    gcomm_throw_fatal << "Up context not defined, released = " 
                              << released;
	}
        
	up_context->handle_up(context_id, rb, roff, up_meta);
    }

    /* apparently passes data buffer to lower layer, what is return value? */
    int pass_down(WriteBuf *wb, const ProtoDownMeta *down_meta)
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

class Toplay : public Protolay
{
    int handle_down(WriteBuf *wb, const ProtoDownMeta *)
    {
	gcomm_throw_fatal << "Toplay handle_down() called";
	throw;
    }
};

class Bottomlay : public Protolay
{
    void handle_up(const int, const ReadBuf *, const size_t,
                   const ProtoUpMeta *)
    {
	gcomm_throw_fatal << "Bottomlay handle_up() called";
    }
};

static inline void connect(Protolay* down, Protolay* up)
{
    down->set_up_context(up);
    up->set_down_context(down);
}

static inline void disconnect(Protolay* down, Protolay* up)
{
    down->change_up_context(up, 0);
    up->change_down_context(down, 0);
}

END_GCOMM_NAMESPACE

#endif /* _GCOMM_PROTOLAY_HPP_ */
