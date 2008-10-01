#ifndef READBUF_HPP
#define READBUF_HPP

#include <gcomm/types.h>
#include <gcomm/exception.hpp>
#include <gcomm/monitor.hpp>
#include <gcomm/logger.hpp>
#include <iostream>

class ReadBuf {
    mutable volatile int refcnt;
    bool instack;
    const unsigned char *buf;
    mutable unsigned char *priv_buf;
    size_t buflen;
    mutable Monitor mon;
    // Private copy operator to disallow assignment 
    ReadBuf operator=(ReadBuf& r) { return r;}
    // Private destructor to disallow allocating ReadBuf from stack


public:


    ~ReadBuf() {
	if (instack == false) {
	    LOG_FATAL("~ReadBuf(): Invalid call to dtor");
	    throw FatalException("Must not call dtor explicitly, object not in stack");
	}
	delete[] priv_buf;
    }

    ReadBuf(const void* buf, const size_t buflen, bool inst) {
	instack = inst;
	refcnt = 1;
	this->buf = reinterpret_cast<const unsigned char *>(buf);
	this->buflen = buflen;
	this->priv_buf = 0;
    }
    
    
    ReadBuf(const void *buf, const size_t buflen) {
	instack = false;
	refcnt = 1;
	this->buf = reinterpret_cast<const unsigned char *>(buf);
	this->buflen = buflen;
	this->priv_buf = 0;
    }

    ReadBuf(const void* bufs[], const size_t buflens[], const size_t nbufs, 
	    const size_t tot_len) {
	instack = false;
	refcnt = 1;
	buf = 0;
	priv_buf = new unsigned char[tot_len];
	buflen = 0;
	for (size_t i = 0; i < nbufs; ++i) {
	    memcpy(priv_buf + buflen, bufs[i], buflens[i]);
	    buflen += buflens[i];
	}
	if (buflen != tot_len)
	    throw FatalException("");
    }
    
    ReadBuf *copy() const {
	if (instack) {
	    ReadBuf* ret = new ReadBuf(get_buf(), get_len());
	    ret->copy();
	    ret->release();
	    return ret;
	} else {
	    Critical crit(&mon);
	    if (priv_buf == 0) {
		priv_buf = new unsigned char[buflen];
		memcpy(priv_buf, buf, buflen);
	    }
	    ++refcnt;
	    return const_cast<ReadBuf*>(this);
	}
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
	mon.enter();
	// std::cerr << "release " << this << " refcnt " << refcnt << "\n";
	assert(refcnt > 0);
	if (--refcnt == 0) {
	    instack = true;
	    mon.leave();
	    delete this; // !!!!
	} else {
	    mon.leave();
	}
    }

    int get_refcnt() const {
	return refcnt;
    }
};

#endif /* READBUF_HPP */
