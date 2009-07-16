#ifndef _GCOMM_WRITEBUF_HPP_
#define _GCOMM_WRITEBUF_HPP_

#include <gcomm/common.hpp>
#include <gcomm/readbuf.hpp>

BEGIN_GCOMM_NAMESPACE

static const size_t WRITEBUF_MAX_HDRLEN = 128;

class WriteBuf
{
    byte_t hdr[WRITEBUF_MAX_HDRLEN];
    size_t hdrlen;
    ReadBuf *rb;
    
    WriteBuf(const WriteBuf&);
    void operator=(const WriteBuf&);
    
    WriteBuf(const byte_t* hdr_, const size_t hdrlen_, const ReadBuf *rb_) :
        hdr(),
        hdrlen(hdrlen_),
        rb(rb_ != 0 ? rb_->copy() : 0)
    {
	memcpy(hdr + (WRITEBUF_MAX_HDRLEN - hdrlen), hdr_, hdrlen);
    }
public:
    
    WriteBuf(const byte_t* buf, const size_t buflen) :
        hdr(),
        hdrlen(0),
        rb(0)
    {
	this->rb = buflen ? new ReadBuf(buf, buflen) : 0;
    }

    WriteBuf(const ReadBuf* rb_) :
        hdr(),
        hdrlen(0),
        rb(0)
    {
        this->rb = rb_ ? rb_->copy() : 0;
    }
    
    ~WriteBuf()
    {
	if (rb)
	    rb->release();
    }
    
    WriteBuf *copy() const 
    {
	return new WriteBuf(get_hdr(), get_hdrlen(), rb);
    }
    
    void prepend_hdr(const void *h, const size_t hl) {
	if (hl + hdrlen > WRITEBUF_MAX_HDRLEN)
	    throw FatalException("out of buffer space");
	
	memcpy(hdr + (WRITEBUF_MAX_HDRLEN - hdrlen - hl), h, hl);
	hdrlen += hl;
    }
    
    void rollback_hdr(const size_t hl) {
	hdrlen -= hl;
    }
    
    const byte_t *get_hdr() const {
	return hdr + (WRITEBUF_MAX_HDRLEN - hdrlen);
    }
    size_t get_hdrlen() const {
	return hdrlen;
    }
    const byte_t *get_buf() const {
	return rb ? rb->get_buf() : 0;
    }

    size_t get_len() const {
	return rb ? rb->get_len() : 0;
    }

    size_t get_totlen() const {
	return get_hdrlen() + get_len();
    }

    ReadBuf *to_readbuf() const {
	const byte_t* bufs[2] = {get_hdr(), rb ? rb->get_buf() : 0};
	size_t buflens[2] = {hdrlen, rb ? rb->get_len() : 0};
	return new ReadBuf(bufs, buflens, rb ? 2 : 1, get_totlen());
    }
    
};


END_GCOMM_NAMESPACE

#endif /* _GCOMM_WRITEBUF_HPP_ */
