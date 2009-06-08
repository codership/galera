#ifndef PROTOLAY_HPP
#define PROTOLAY_HPP

#include <gcomm/common.hpp>
#include <gcomm/util.hpp>
#include <gcomm/readbuf.hpp>
#include <gcomm/writebuf.hpp>
#include <gcomm/view.hpp>
#include <gcomm/exception.hpp>
#include <gcomm/logger.hpp>

#include <cerrno>

BEGIN_GCOMM_NAMESPACE

class ProtoUpMeta
{
    const UUID source;
    const uint8_t user_type;
    const int64_t to_seq;
    View* const view;
public:
    ProtoUpMeta() :
        source(UUID()),
        user_type(0xff),
        to_seq(-1),
        view(0)
    {
    }
    
    ProtoUpMeta(const ProtoUpMeta& um) :
        source(um.source),
        user_type(um.user_type),
        to_seq(um.to_seq),
        view(um.view ? new View(*um.view) : 0)
    {

    }

    ProtoUpMeta(const View* view_, const int64_t to_seq_ = -1) :
        source(),
        user_type(0xff),
        to_seq(to_seq_),
        view(new View(*view_))
    {
        
    }
    
    ProtoUpMeta(const UUID& source_, const uint8_t user_type_ = 0xff,
                const int64_t to_seq_ = -1) :
        source(source_),
        user_type(user_type_),
        to_seq(to_seq_),
        view(0)
    {
        
    }


    ~ProtoUpMeta()
    {
        delete view;
    }

    const UUID& get_source() const
    {
        return source;
    }

    uint8_t get_user_type() const
    {
        return user_type;
    }

    int64_t get_to_seq() const
    {
        return to_seq;
    }

    const View* get_view() const
    {
        return view;
    }
};

class ProtoDownMeta
{
    const uint8_t user_type;
public:
    ProtoDownMeta(const uint8_t user_type_) :
        user_type(user_type_)
    {
    }

    uint8_t get_user_type() const
    {
        return user_type;
    }
};

class Protolay {
    int context_id;
    Protolay *up_context;
    Protolay *down_context;
    bool released;
protected:
    Protolay() : 
        context_id(-1),
        up_context(0),
        down_context(0),
        released(false)
    {
    }

public:
    void release()
    {
        up_context = 0;
        down_context = 0;
        released = true;
    }
    
    virtual ~Protolay()
    {
    }

    virtual int handle_down(WriteBuf *, const ProtoDownMeta*) = 0;
    virtual void handle_up(const int cid, const ReadBuf *, const size_t, const ProtoUpMeta*) = 0;

    
    void set_up_context(Protolay *up)
    {
	if (up_context) {
	    Logger::instance().fatal("Protolay::set_up_context(): "
				     "Context already exists");
	    throw FatalException("Up context already exists");
	}
	up_context = up;
    }
    
    void set_up_context(Protolay *up, const int id)
    {
	if (up_context) {
	    LOG_FATAL("Protolay::set_up_context(): Context already exists");
	    throw FatalException("Up context already exists");	   
	}
	context_id = id;
	up_context = up;
    }

    void change_up_context(Protolay* old_up, Protolay* new_up)
    {
        if (up_context != old_up) {
            LOG_FATAL("Protolay::change_up_context(): old context does not match: " 
                      + Pointer(old_up).to_string() 
                      + " " 
                      + Pointer(up_context).to_string());
            throw FatalException("context mismatch");
        }
        
        up_context = new_up;
    }

    void change_down_context(Protolay* old_down, Protolay* new_down)
    {
        if (down_context != old_down) {
            LOG_FATAL("Protolay::change_down_context(): old context does not match: " 
                      + Pointer(old_down).to_string() 
                      + " " 
                      + Pointer(down_context).to_string());
            throw FatalException("context mismatch");
        }
        down_context = new_down;
    }

    void set_down_context(Protolay *down, const int id) {
	context_id = id;
	down_context = down;
    }

    void set_down_context(Protolay *down) {
	down_context = down;
    }
    
    void pass_up(const ReadBuf *rb, const size_t roff, 
		 const ProtoUpMeta *up_meta) {
	if (!up_context) {
	    LOG_FATAL("Protolay::pass_up(): "
		      "Up context not defined, released = " 
                      + Bool(released).to_string());
	    throw FatalException("Up context not defined");
	}
        
	up_context->handle_up(context_id, rb, roff, up_meta);
    }

    int pass_down(WriteBuf *wb, const ProtoDownMeta *down_meta) {
	if (!down_context) {
	    LOG_FATAL("Protolay::pass_down(): Down context not defined, "
                      "released = " + Bool(released).to_string());
	    throw FatalException("Down context not defined");
	}

	// Simple guard of wb consistency in form of testing 
	// writebuffer header length. 
	size_t down_hdrlen = wb->get_hdrlen();
	int ret = down_context->handle_down(wb, down_meta);
	if (down_hdrlen != wb->get_hdrlen())
	    throw FatalException("hdr not rolled back");
	return ret;
    }    
};

class Toplay : public Protolay {
    int handle_down(WriteBuf *wb, const ProtoDownMeta *) {
	throw FatalException("Toplay handle_down() called");
	return 0;
    }
};

class Bottomlay : public Protolay {
    void handle_up(const int, const ReadBuf *, const size_t, const ProtoUpMeta *) {
	throw FatalException("Bottomlay handle_up() called");
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

#endif /* PROTOLAY_HPP */
