#ifndef READBUF_HPP
#define READBUF_HPP

#include <gcomm/types.h>
#include <gcomm/exception.hpp>
#include <iostream>

class ReadBuf {
    mutable volatile int refcnt;
    const unsigned char *buf;
    mutable unsigned char *priv_buf;
    size_t buflen;
    // Private copy operator to disallow assignment 
    ReadBuf operator=(ReadBuf& r) { return r;}
    // Private destructor to disallow allocating ReadBuf from stack
    ~ReadBuf() {
	delete[] priv_buf;
    }
public:
    ReadBuf(const void *buf, const size_t buflen) {
	refcnt = 1;
	this->buf = reinterpret_cast<const unsigned char *>(buf);
	this->buflen = buflen;
	this->priv_buf = 0;
    }
    
    ReadBuf *copy() const {
	if (priv_buf == 0) {
	    priv_buf = new unsigned char[buflen];
	    memcpy(priv_buf, buf, buflen);
	}
	++refcnt;
	return const_cast<ReadBuf*>(this);
    }
    
    
    ReadBuf *copy(const size_t offset) const {
	if (offset > buflen)
	    throw DException("");
	if (offset > 0) {
	    ReadBuf *ret = new ReadBuf(get_buf(offset), buflen - offset);
	    ret->copy();
	    ret->release();
	    return ret;
	} else {
	    return copy();
	}
    }

    const void *get_buf(const size_t offset) const {
	if (offset > buflen)
	    throw FatalException("Trying to read past buffer end");
	return (priv_buf ? priv_buf : buf) + offset;
    }
    
    const void *get_buf() const {
	return priv_buf ? priv_buf : buf;
    }
    
    size_t get_len() const {
	return buflen;
    }
    
    size_t get_len(size_t off) const {
	if (off > buflen)
	    throw FatalException("Offset greater than buffer length");
	return buflen - off;
    }

    void release() {
	// std::cerr << "release " << this << " refcnt " << refcnt << "\n";
	if (--refcnt == 0)
	    delete this; // !!!!
    }

    int get_refcnt() const {
	return refcnt;
    }
};

#endif /* READBUF_HPP */
