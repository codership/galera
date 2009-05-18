#ifndef WRITEBUF_HPP
#define WRITEBUF_HPP

#include <gcomm/common.hpp>
#include <gcomm/readbuf.hpp>

BEGIN_GCOMM_NAMESPACE

static const size_t WRITEBUF_MAX_HDRLEN = 128;

class WriteBuf
{
    unsigned char hdr[WRITEBUF_MAX_HDRLEN];
    size_t hdrlen;
    ReadBuf *rb;
    
    WriteBuf& operator=(WriteBuf& w) {return w;}

    WriteBuf(const void *hdr, const size_t hdrlen, ReadBuf *rb) {
	memcpy(this->hdr + (WRITEBUF_MAX_HDRLEN - hdrlen), hdr, hdrlen);
	this->hdrlen = hdrlen;
	this->rb = rb ? rb->copy() : 0;
    }
public:

    WriteBuf(const void *buf, const size_t buflen) {
	this->rb = buflen ? new ReadBuf(buf, buflen) : 0;
	this->hdrlen = 0;
    }

    WriteBuf(const ReadBuf* rb)
    {
        this->rb = rb ? rb->copy() : 0;
        this->hdrlen = 0;
    }

    ~WriteBuf() {
	if (rb)
	    rb->release();
    }
    
    WriteBuf *copy() const {
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
    
    const void *get_hdr() const {
	return hdr + (WRITEBUF_MAX_HDRLEN - hdrlen);
    }
    size_t get_hdrlen() const {
	return hdrlen;
    }
    const void *get_buf() const {
	return rb ? rb->get_buf() : 0;
    }

    size_t get_len() const {
	return rb ? rb->get_len() : 0;
    }

    size_t get_totlen() const {
	return get_hdrlen() + get_len();
    }

    ReadBuf *to_readbuf() const {
	const void* bufs[2] = {get_hdr(), rb ? rb->get_buf() : 0};
	size_t buflens[2] = {hdrlen, rb ? rb->get_len() : 0};
	return new ReadBuf(bufs, buflens, rb ? 2 : 1, get_totlen());
    }
    
};


END_GCOMM_NAMESPACE

#endif /* WRITEBUF_HPP */
