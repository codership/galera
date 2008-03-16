#ifndef WRITEBUF_HPP
#define WRITEBUF_HPP

#include <gcomm/types.h>
#include <gcomm/readbuf.hpp>

static const size_t WRITEBUF_MAX_HDRLEN = 64;

class WriteBuf {
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

    ~WriteBuf() {
	if (rb)
	    rb->release();
    }
    
    WriteBuf *copy() const {
	return new WriteBuf(get_hdr(), get_hdrlen(), rb);
    }
    
    void prepend_hdr(const void *h, const size_t hl) {
	if (hl + hdrlen > WRITEBUF_MAX_HDRLEN)
	    throw std::exception();
	
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

    /* TODO: This method uses one unnecessary temporary buffer, optimize */
    ReadBuf *to_readbuf() const {
	unsigned char *tmp_buf = new unsigned char[get_totlen()];
	memcpy(tmp_buf, get_hdr(), get_hdrlen());
	memcpy(tmp_buf + get_hdrlen(), get_buf(), get_len());
	ReadBuf *rb = new ReadBuf(tmp_buf, get_totlen());
	ReadBuf *ret = rb->copy();
	rb->release();
	delete[] tmp_buf;
	return ret;
    }

};




#endif /* WRITEBUF_HPP */
