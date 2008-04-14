

#include "gcomm/transport.hpp"
#include "vs_backend.hpp"
// Message and command definitions
#include "vs_remote_backend.hpp"

#include <list>
#include <cstring>
#include <iostream>
#include <sstream>
#include <sys/time.h>


class Logger {
    std::ostream& ost;
public:
    Logger() : ost(std::cerr) {}
    Logger(std::ostream& o) : ost(o) {}
    void log(int level, const char *msg) {
	timeval tv;
	::gettimeofday(&tv, 0);
	ost << tv.tv_sec << "." << tv.tv_usec << ": " << msg << "\n";
    }
    void log(int level, std::string msg) {
	log(level, msg.c_str());
    }
};

static Logger log;

#define DLOG(a) do {				\
	std::string _sb;			\
	std::ostringstream _os(_sb);		\
	_os << a;				\
	log.log(1, _os.str());			\
    } while (0)


class Stats {
    unsigned long long n_msgs_out;
    unsigned long long bytes_out;
    unsigned long long n_msgs_in;
    unsigned long long bytes_in;
    unsigned long long tlast;
public:
    Stats() : n_msgs_out(0), bytes_out(0), n_msgs_in(0), bytes_in(0) {
	struct timeval tv;
	::gettimeofday(&tv, 0);
	tlast = tv.tv_sec, tlast *= 1000000;
	tlast += tv.tv_usec;
    }
    void print() {
	struct timeval tv;
	::gettimeofday(&tv, 0);
	unsigned long long tnow = tv.tv_sec;
	tnow *= 1000000;
	tnow += tv.tv_usec;
	if (tlast + 5000000 < tnow) {
	    DLOG("Queued " << n_msgs_in << " msgs " << bytes_in << " bytes ");
	    DLOG("    " << 1.e6*double(n_msgs_in)/(tnow - tlast) << " msg/sec");
	    DLOG("    " << 1.e6*double(bytes_in)/(tnow - tlast) << " bytes/sec");
	    DLOG("Forwarded " << n_msgs_out << " msgs " << bytes_out << " bytes ");
	    DLOG("    " << 1.e6*double(n_msgs_out)/(tnow - tlast) << " msg/sec");
	    DLOG("    " << 1.e6*double(bytes_out)/(tnow - tlast) << " bytes/sec");
	    tlast = tnow;
	    n_msgs_out = bytes_out = n_msgs_in = bytes_in = 0;
	}
    }

    void incr_out(size_t b) {
	n_msgs_out++;
	bytes_out += b;
	print();
    }
    void incr_in(size_t b) {
	n_msgs_in++;
	bytes_in += b;
    }
};

static Stats stats;

class ClientHandler : public Toplay {
    VSBackend *vs;
    Transport *tp;
public:
    enum State {CLOSED, HANDSHAKE, CONNECTED} state;
    enum {
	TP, VS
    };
    ClientHandler(Transport *t, VSBackend *v) : vs(v), tp(t) {
	state = HANDSHAKE;
	tp->set_up_context(this, TP);
	vs->set_up_context(this, VS);
    }
    ~ClientHandler() {
	if (vs)
	    DLOG("deleting " << vs->get_self());
	delete tp;
	delete vs;
    }

    State get_state() const {
	return state;
    }
    
    void close() {
	vs->close();
	tp->close();
	state = CLOSED;
    }

    void handle_vs(const ReadBuf *rb, const size_t roff,
		   const ProtoUpMeta *um) {
	if (rb == 0) {
	    DLOG("Null message, silent drop");
	} else {
	    WriteBuf wb(rb->get_buf(roff), rb->get_len(roff));
	    VSRMessage msg;
	    wb.prepend_hdr(msg.get_raw(), msg.get_raw_len());
	    int err;
	    if ((err = tp->handle_down(&wb, 0)) == EAGAIN) {
		DLOG("Fixme!!!");
		close();
		return;
	    } else if (err) {
		DLOG("Error: " << strerror(err));
		close();
		return;
	    }
	    stats.incr_out(rb->get_len(roff));
	}
	
    }
    
    void handle_tp(const ReadBuf *rb, const size_t roff,
		   const ProtoUpMeta *um) {
	VSRMessage msg;
	if (rb == 0 && tp->get_state() == TRANSPORT_S_FAILED) {
	    DLOG("rb = 0 and tp state = failed"); 
	    close();
	    return;
	} else if (rb == 0 && tp->get_state() == TRANSPORT_S_CLOSED) {
	    close();
	    return;
	} else if (rb == 0 && state == HANDSHAKE) {
	    // Should not happen because Transport::accept() results 
	    // connected socket (at least should)
	    throw DException("");
	    try {
		vs->connect("fifo");
		VSRMessage rmsg(vs->get_self());
		WriteBuf wb(0, 0);
		wb.prepend_hdr(rmsg.get_raw(),rmsg.get_raw_len());
		if (tp->handle_down(&wb, 0)) {
		    DLOG("error: handle_down()");
		    close();
		}
		return;
	    } catch (Exception e) {
		DLOG("Exception: " << e.what() << "\n");
		close();
		return;
	    }
	    
	} else if (rb == 0) {
	    throw DException("What just happened?");
	} else {
	    if (msg.read(rb->get_buf(), rb->get_len(), roff) == 0) {
		DLOG("Invalid message");
		close();
		return;
	    }
	}
	
	// DLOG("State: " << state << " Message " << msg.get_type());
	
	switch (state) {
	case HANDSHAKE: {
	    if (msg.get_type() != VSRMessage::CONTROL) {
		DLOG("Invalid message sequence: state " << state << " " << "message " << msg.get_type());
		close();
		return;
	    }
	    VSRCommand cmd = msg.get_command();
	    if (cmd.get_type() != VSRCommand::SET) {
		DLOG("Invalid message sequence");
		close();
		return;
	    }
	    state = CONNECTED;
	    VSRCommand respcmd(VSRCommand::RESULT, VSRCommand::SUCCESS);
	    VSRMessage resp(respcmd);
	    WriteBuf wb(0, 0);
	    wb.prepend_hdr(resp.get_raw(), resp.get_raw_len());
	    
	    if (tp->handle_down(&wb, 0)) {
		DLOG("Fixme!!!");
		close();
	    }
	    break;
	}
	case CONNECTED: {
	    if (msg.get_type() == VSRMessage::CONTROL) {
		VSRCommand cmd = msg.get_command();
		WriteBuf wb(0, 0);
		if (cmd.get_type() == VSRCommand::JOIN || cmd.get_type() == VSRCommand::LEAVE) {
		    DLOG("Cmd " << cmd.get_type() << " " << cmd.get_address());
		    try {
			if (cmd.get_type() == VSRCommand::JOIN)
			    vs->join(cmd.get_address().get_service_id());
			else
			    vs->leave(cmd.get_address().get_service_id());
			VSRCommand response(VSRCommand::RESULT, VSRCommand::SUCCESS);
			VSRMessage rmsg(response);
			wb.prepend_hdr(rmsg.get_raw(), rmsg.get_raw_len());
		    } catch (Exception e) {
			DLOG(e.what());
			VSRCommand response(VSRCommand::RESULT, VSRCommand::FAIL);
			VSRMessage rmsg(response);
			
			wb.prepend_hdr(rmsg.get_raw(), rmsg.get_raw_len());
		    }
		}
		tp->handle_down(&wb, 0);
	    } else if (msg.get_type() == VSRMessage::VSPROTO) {
		WriteBuf wb(rb->get_buf(roff + msg.get_raw_len()), rb->get_len(roff + msg.get_raw_len()));
		vs->handle_down(&wb, 0);
		stats.incr_in(rb->get_len(roff + msg.get_raw_len()));
	    } else {
		DLOG("Invalid message");
		close();
		return;
	    }
	    break;
	}
	case CLOSED:
	    throw DException("");
	    break;
	}
    }

    void handle_up(const int cid, const ReadBuf *rb, const size_t roff, 
		   const ProtoUpMeta *um) {
	switch (cid) {
	case VS:
	    handle_vs(rb, roff, um);
	    break;
	case TP:
	    handle_tp(rb, roff, um);
	}
    }

    void start() {
	try {
	    vs->connect("fifo");
	    VSRMessage rmsg(vs->get_self());
	    WriteBuf wb(0, 0);
	    wb.prepend_hdr(rmsg.get_raw(),rmsg.get_raw_len());
	    if (tp->handle_down(&wb, 0)) {
		close();
	    } else {
		DLOG("Sent handshake");
	    }
	    return;
	} catch (Exception e) {
	    DLOG("Exception: " << e.what());
	    close();
	    return;
	}
    }

};


class Server : public Toplay {
    std::list<ClientHandler *> clients;
    Transport *listener;
    char *addr;
    Poll *tp_poll;
    Poll *fifo_poll;
    bool terminate;
public:
    Server(const char *a) : listener(0), terminate(false) {
	addr = ::strdup(a);
	tp_poll = Poll::create("def");
	fifo_poll = Poll::create("fifo");
    }
    
    void handle_up(const int cid, const ReadBuf *rb, const size_t roff, 
		   const ProtoUpMeta *um) {
	Transport *tp = listener->accept(tp_poll);
	tp->set_contention_params(1, 5);
	VSBackend *vs = VSBackend::create("fifo", fifo_poll);
	ClientHandler *cl = new ClientHandler(tp, vs);
	clients.push_back(cl);
	cl->start();
    }
    

    
    void start() {
	listener = Transport::create(addr, tp_poll, this);
	listener->listen(addr);
    }
    
    void stop() {
	listener->close();
	delete listener;
	terminate = true;
    }
    
    void cleanup() {
	std::list<ClientHandler *>::iterator i, i_next;
	for (i = clients.begin(); i != clients.end(); i = i_next) {
	    i_next = i, ++i_next;
	    if ((*i)->get_state() == ClientHandler::CLOSED) {
		delete *i;
		clients.erase(i);
	    }
	}
    }
    
    int run() {
	int err;
	start();
	terminate = false;
	while ((err = tp_poll->poll(1000)) >= 0 && terminate == false) {
	    while ((err = fifo_poll->poll(0)) > 0);
	    if (err < 0 ) {
		terminate = true;
		break;
	    }
	    cleanup();
	}
	stop();
	return err;
    }
};


#ifdef COMPILE_SERVER

#include <csignal>
#include <string>
using namespace std;

int main(int argc, char *argv[])
{
    log.log(1, "start");
    ::signal(SIGPIPE, SIG_IGN);

    if (argc < 2) {
	cerr << "Usage: " << argv[0] << " <address>" << endl;
	exit (-1);
    }
//    try {
	Server s(argv[1]);
	s.run();
//    } catch (Exception e) {
//	std::cerr << e.what() << "\n";
//	return 1;
	//   }
    return 0;
}

#endif // COMPILE_SERVER
