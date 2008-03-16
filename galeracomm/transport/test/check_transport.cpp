
extern "C" {
#include "gcomm/poll.h"
#include "gcomm/transport.h"
}

#include <cerrno>
#include <cstdio>
#include <exception>
#include <string>
#include <iostream>

class Exception : public std::exception {
    std::string reason;
public:
    Exception(const char *str) : reason(str) {}
    ~Exception() throw() {}
    const char *what() const throw() {
	return reason.c_str();
    }
};

const char *addr = "127.0.0.1:25678";
/* Message producer and consumer */
class MessagePC {
    transport_t *snd;
    transport_t *rcv;
    transport_t *listen;
    protolay_t *pl;
    enum State {CONNECTING, CONNECTED} state;
    
    readbuf_t *recv_msg;
    size_t tport_offset;
    size_t snd_size;
    char *snd_buf;
    poll_t *poll;
public:
    MessagePC() : snd(0), rcv(0), listen(0), state(CONNECTING), 
		  recv_msg(0), snd_size(0), snd_buf(0) {
	poll = poll_new();
    }
    ~MessagePC() {}
     
    void start_listen() throw() {
	int ret;
	listen = transport_new(TRANSPORT_TCP, 0, 0, 0);
	ret = transport_listen(listen, addr);
	poll_insert(poll, transport_fd(listen), this, &event_cb);
	poll_set(poll, transport_fd(listen), POLL_IN);
	if (ret != 0)
	    throw Exception("Transport listen returned error");
    }
    

    static void pl_up_cb(protolay_t *pl, const readbuf_t *rb, 
			 const size_t roff, const up_meta_t *up_meta) {
	MessagePC *mpc = reinterpret_cast<MessagePC*>(
	    protolay_get_priv(pl));
	if (mpc->rcv == 0 || mpc->snd == 0)
	    throw Exception("Wtf in pl_up_cb");
	if (rb == 0 && transport_get_state(mpc->snd) == TRANSPORT_S_CONNECTED) {
	    mpc->state = CONNECTED;
	} else if (rb && transport_get_state(mpc->rcv) == TRANSPORT_S_CONNECTED) {
	    mpc->recv_msg = readbuf_copy(rb);
	    mpc->tport_offset = roff;
	} else if (transport_get_state(mpc->snd) == TRANSPORT_S_FAILED ||
		   transport_get_state(mpc->rcv) == TRANSPORT_S_FAILED) {
	    throw Exception("Transport has failed");
	} else {
	    std::cout << "Unknown event \n";
	}
    }
    
    void start_snd() throw() {
	int ret;
	pl = protolay_new(this, NULL);
	snd = transport_new(TRANSPORT_TCP, poll, pl, &pl_up_cb);
	ret = transport_connect(snd, addr);
	if (ret != 0 && ret != EINPROGRESS)
	    throw Exception("Transport connect returned error");
    }
    
    static void event_cb(void *ctx, int fd, poll_e ev) {
	MessagePC *mpc = reinterpret_cast<MessagePC *>(ctx);
	if (fd == transport_fd(mpc->listen) && (ev & POLL_IN) &&
	    transport_accept(mpc->listen, &mpc->rcv, mpc->poll,
			     mpc->pl, &pl_up_cb) == 0) {
	    poll_unset(mpc->poll, transport_fd(mpc->listen), POLL_IN);
	    std::cout << "Accept\n";
	} else {
	    throw Exception("Unexpected event in listener cb");
	}
    }

    void connect() {
	while (state == CONNECTING) {
	    std::cout << "poll\n";
	    if (poll_until(poll, 1000))
		throw Exception("Poll failed");
	}
	poll_erase(poll, transport_fd(listen));
    }
     
    void check(const size_t s) throw () {
	writebuf_t *snd_msg;
	size_t i;
	
	snd_size = s;
	snd_buf = new char[s];
	for (i = 0; i < s; i++)
	    snd_buf[i] = rand()%256;
	
	snd_msg = writebuf_new(snd_buf, s);
	printf("Sending %u bytes\n" , s);	
	transport_send(snd, snd_msg);
	writebuf_free(snd_msg);
	
	do {
	    if (poll_until(poll, 1000))
		throw Exception("Poll failed");
	} while (recv_msg == 0);
	
	readbuf_free(recv_msg);
	recv_msg = 0;
	delete[] snd_buf;
    }
    
    void cleanup() {
	poll_erase(poll, transport_fd(listen));
	transport_close(listen);
	transport_free(listen);
	
	transport_close(rcv);
	transport_free(rcv);
	
	transport_close(snd);
	transport_free(snd);
	poll_free(poll);
    }
};


int main()
{
     MessagePC mpc;

     mpc.start_listen();
     std::cout << "Listening\n";
     mpc.start_snd();
     std::cout << "Connecting\n";
     mpc.connect();
     std::cout << "Connected\n";
     for (size_t i = 1; i <= (1 << 20); i <<= 1)  {
	  mpc.check(i);
     }

     mpc.cleanup();
     
     return 0;
}
