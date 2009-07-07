#ifndef READBUF_HPP
#define READBUF_HPP

#include <gcomm/common.hpp>
#include <gcomm/exception.hpp>
#include <gcomm/monitor.hpp>
#include <gcomm/logger.hpp>
#include <iostream>

#include <cstring>

#include <map>
using std::map;

BEGIN_GCOMM_NAMESPACE

class Buffer
{
    byte_t* buf;
    size_t buflen;
    Buffer(const Buffer&);
    void operator=(const Buffer&);
public:
    Buffer(const size_t buflen_) : 
        buf(),
        buflen(buflen_)
    {
        buf = new byte_t[buflen];
    }

    ~Buffer()
    {
        delete[] buf;
    }

    byte_t* get_buf() const
    {
        return buf;
    }

    size_t get_len() const
    {
        return buflen;
    }

};


#if 1

class ReadBuf
{
    mutable volatile int refcnt;
    bool instack;
    const byte_t* buf;
    mutable byte_t* priv_buf;
    size_t buflen;
    mutable Monitor mon;

    ReadBuf(const ReadBuf&);
    // Private copy operator to disallow assignment 
    void operator=(ReadBuf& r);



public:


    ~ReadBuf() 
    {
	if (instack == false) {
	    LOG_FATAL("~ReadBuf(): Invalid call to dtor");
	    throw FatalException("Must not call dtor explicitly, object not in stack");
	}
	delete[] priv_buf;
    }
    
    ReadBuf(const byte_t* buf_, const size_t buflen_, bool instack_ = false) :
        refcnt(1),
        instack(instack_),
        buf(buf_),
        priv_buf(0),
        buflen(buflen_),
        mon()
    {

    }
    
    


    ReadBuf(const byte_t* bufs[], const size_t buflens[], 
            const size_t nbufs, 
	    const size_t tot_len) :
        refcnt(1),
        instack(false),
        buf(0),
        priv_buf(0),
        buflen(0),
        mon()
    {
	priv_buf = new byte_t[tot_len];
	for (size_t i = 0; i < nbufs; ++i)
        {
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
		priv_buf = new byte_t[buflen];
		memcpy(priv_buf, buf, buflen);
	    }
	    ++refcnt;
	    return const_cast<ReadBuf*>(this);
	}
    }
    
    
    ReadBuf *copy(const size_t offset) const {
	if (offset > buflen)
	    throw FatalException("");
	if (offset > 0) {
	    ReadBuf *ret = new ReadBuf(get_buf(offset), buflen - offset);
	    ret->copy();
	    ret->release();
	    return ret;
	} else {
	    return copy();
	}
    }

    const byte_t *get_buf(const size_t offset) const {
	if (offset > buflen)
	    throw FatalException("Trying to read past buffer end");
	return (priv_buf ? priv_buf : buf) + offset;
    }
    
    const byte_t *get_buf() const {
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

#else




class ReadBuf
{
    byte_t* buf;
    size_t buflen;

public:
    
    
    ~ReadBuf() {
	delete[] buf;
    }

    ReadBuf(const byte_t* buf, const size_t buflen, bool inst) 
    {
	this->buf = new byte_t[buflen];
        memcpy(this->buf, buf, buflen); 
	this->buflen = buflen;
    }
    
    
    ReadBuf(const byte_t *buf, const size_t buflen) 
    {
	this->buf = new byte_t[buflen];
        memcpy(this->buf, buf, buflen); 
	this->buflen = buflen;
    }

    ReadBuf(const byte_t* bufs[], const size_t buflens[], 
            const size_t nbufs, 
	    const size_t tot_len) {
        buf = new byte_t[tot_len];
	buflen = 0;
	for (size_t i = 0; i < nbufs; ++i) {
	    memcpy(buf + buflen, bufs[i], buflens[i]);
	    buflen += buflens[i];
	}
	if (buflen != tot_len)
	    throw FatalException("");
    }
    
    ReadBuf *copy() const {
        return new ReadBuf(buf, buflen);
    }
    
    
    ReadBuf *copy(const size_t offset) const {
	if (offset > buflen)
	    throw FatalException("");
        return new ReadBuf(get_buf(offset), buflen - offset);
    }

    const byte_t *get_buf(const size_t offset) const 
    {
	if (offset > buflen)
	    throw FatalException("Trying to read past buffer end");
	return buf + offset;
    }
    
    const byte_t *get_buf() const 
    {
	return buf;
    }
    
    size_t get_len() const 
    {
	return buflen;
    }
    
    size_t get_len(size_t off) const {
	if (off > buflen)
	    throw FatalException("Offset greater than buffer length");
	return buflen - off;
    }
    
    void release() 
    {
        memset(buf, 0xff, buflen);
        delete this;
    }
    
    int get_refcnt() const 
    {
        return 1;
    }
};


#endif



#include <cstdio>
#include <ctype.h>

static inline void dump(const ReadBuf* rb)
{
//    fprintf(stderr, "rb buf ptr %p, len %lu\n\n",
//            rb->get_buf(), rb->get_len());
    fprintf(stderr, "*******************dump********************\n\n");
    for (size_t i = 0; i < rb->get_len(); ++i)
    {
        fprintf(stderr, "%2.2x ", *rb->get_buf(i));
        if (i % 16 == 15)
            fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n\n");
    for (size_t i = 0; i < rb->get_len(); ++i)
    {
        if (isalnum(*rb->get_buf(i)))
            fprintf(stderr, "%c", *rb->get_buf(i));
        else
            fprintf(stderr, ".");
    }
    fprintf(stderr, "\n\n");    
    fprintf(stderr, "*******************dump********************\n\n");
}


END_GCOMM_NAMESPACE

#endif /* READBUF_HPP */
