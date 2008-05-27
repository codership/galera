#ifndef PROTOLAY_HPP
#define PROTOLAY_HPP


#include <gcomm/poll.hpp>
#include <gcomm/readbuf.hpp>
#include <gcomm/writebuf.hpp>
#include <gcomm/exception.hpp>
#include <gcomm/logger.hpp>

#include <cerrno>

struct ProtoUpMeta {
};

struct ProtoDownMeta {
};

class Protolay {
    int context_id;
    Protolay *up_context;
    Protolay *down_context;
//    Poll *poll;
//    PollContext *poll_context;
protected:
    Protolay() : context_id(-1), up_context(0), down_context(0) {}

//     Protolay(Poll *p, PollContext *pctx) : 
// 	context_id(-1), up_context(0), down_context(0),
// 	poll(p), poll_context(pctx) {}



public:
    virtual ~Protolay() {}

    virtual int handle_down(WriteBuf *, const ProtoDownMeta *) = 0;
    virtual void handle_up(const int cid, const ReadBuf *, const size_t, const ProtoUpMeta *) = 0;
    
    void set_up_context(Protolay *up) {
	if (up_context) {
	    Logger::instance().fatal("Protolay::set_up_context(): "
				     "Context already exists");
	    throw FatalException("Up context already exists");
	}
	up_context = up;
    }
    
    void set_up_context(Protolay *up, const int id) {
	if (up_context) {
	    Logger::instance().fatal("Protolay::set_up_context(): "
				     "Context already exists");
	    throw FatalException("Up context already exists");	   
	}
	context_id = id;
	up_context = up;
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
		      "Up context not defined");
	    throw FatalException("Up context not defined");
	}
	    
	up_context->handle_up(context_id, rb, roff, up_meta);
    }

    int pass_down(WriteBuf *wb, const ProtoDownMeta *down_meta) {
	if (!down_context) {
	    LOG_FATAL("Protolay::pass_down(): Down context not defined");
	    throw FatalException("Down context not defined");
	}

	// Simple guard of wb consistency in form of testing 
	// writebuffer header length. 
	size_t down_hdrlen = wb->get_hdrlen();
	int ret = down_context->handle_down(wb, down_meta);
	if (down_hdrlen != wb->get_hdrlen())
	    throw DException("");
	return ret;
    }
    

//     void set_poll_up(const int fd, const bool b) {
// 	if (poll) {
// 	    if (b)
// 		poll->set(fd, PollEvent::POLL_IN);
// 	    else
// 		poll->unset(fd, PollEvent::POLL_IN);
// 	} else if (down_context) {
// 	    down_context->set_poll_up(fd, b);
// 	}
//     }

//     void set_poll_down(const int fd, const bool b) {
// 	if (poll) {
// 	    if (b)
// 		poll->set(fd, PollEvent::POLL_OUT);
// 	    else
// 		poll->unset(fd, PollEvent::POLL_OUT);
// 	} else if (down_context) {
// 	    down_context->set_poll_down(fd, b);
// 	}
//     }

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



class Bridge : public Protolay {


public: 
    Bridge(Protolay *p0, Protolay *p1) {
	p0->set_up_context(this, 0);
	p1->set_up_context(this, 1);
    }
};


#endif /* PROTOLAY_HPP */
