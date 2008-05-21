
#include "vsbes.hpp"
#include "gcomm/logger.hpp"

// Message and command definitions
#include "vs_remote_backend.hpp"


#include <cstring>
#include <iostream>
#include <sstream>
#include <sys/time.h>

static Logger& logger = Logger::instance();


class Stats {
//    unsigned long long n_msgs_out;
//    unsigned long long bytes_out;
//    unsigned long long n_msgs_in;
//    unsigned long long bytes_in;
//    unsigned long long tlast;
    uint64_t n_msgs_out;
    uint64_t bytes_out;
    uint64_t n_msgs_in;
    uint64_t bytes_in;
    uint64_t tlast;
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
	    logger.info(std::string("Queued ") + to_string(n_msgs_in) 
			+ " msgs " + to_string(bytes_in) + " bytes ");
	    logger.info(std::string("    ") 
			+ to_string(1.e6*double(n_msgs_in)/(tnow - tlast)) 
			+ " msg/sec");
	    logger.info(std::string("    ") 
			+ to_string(1.e6*double(bytes_in)/(tnow - tlast)) + " bytes/sec");
	    logger.info(std::string("Forwarded ") 
			+ to_string(n_msgs_out) + " msgs " 
			+ to_string(bytes_out) + " bytes ");
	    logger.info(std::string("    ") 
			+ to_string(1.e6*double(n_msgs_out)/(tnow - tlast)) 
			+ " msg/sec");
	    logger.info(std::string("    ") 
			+ to_string(1.e6*double(bytes_out)/(tnow - tlast)) 
			+ " bytes/sec");
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


ClientHandler::ClientHandler(Transport *t, VSBackend *v) : vs(v), tp(t)
{
    state = HANDSHAKE;
    tp->set_up_context(this, TP);
    vs->set_up_context(this, VS);
}

ClientHandler::~ClientHandler()
{
    if (vs)
	logger.info(std::string("deleting ") + vs->get_self().to_string());
    delete tp;
    delete vs;
}

ClientHandler::State ClientHandler::get_state() const 
{
    return state;
}

void ClientHandler::close() 
{
    vs->close();
    tp->close();
    state = CLOSED;
}

void ClientHandler::handle_vs(const ReadBuf *rb, const size_t roff,
			      const ProtoUpMeta *um)
{
    if (rb == 0) {
	logger.info("Null message, silent drop");
    } else {
	WriteBuf wb(rb->get_buf(roff), rb->get_len(roff));
	VSRMessage msg;
	wb.prepend_hdr(msg.get_raw(), msg.get_raw_len());
	int err;
	if ((err = tp->handle_down(&wb, 0)) == EAGAIN) {
	    logger.info("Fixme!!!");
	    close();
	    return;
	} else if (err) {
	    logger.error(std::string("Error: ") + strerror(err));
	    close();
	    return;
	}
	stats.incr_out(rb->get_len(roff));
    }    
}
    
void ClientHandler::handle_tp(const ReadBuf *rb, const size_t roff,
			      const ProtoUpMeta *um)
{
    VSRMessage msg;
    if (rb == 0 && tp->get_state() == TRANSPORT_S_FAILED) {
	logger.debug("rb = 0 and tp state = failed"); 
	close();
	return;
    } else if (rb == 0 && tp->get_state() == TRANSPORT_S_CLOSED) {
	close();
	return;
    } else if (rb == 0 && state == HANDSHAKE) {
	// Should not happen because Transport::accept() results 
	// connected socket (at least should)
	throw FatalException("");
	try {
	    vs->connect("fifo");
	    VSRMessage rmsg(vs->get_self());
	    WriteBuf wb(0, 0);
	    wb.prepend_hdr(rmsg.get_raw(),rmsg.get_raw_len());
	    if (tp->handle_down(&wb, 0)) {
		logger.info("error: handle_down()");
		close();
	    }
	    return;
	} catch (Exception e) {
	    logger.error(std::string("Exception: ") + e.what());
	    close();
	    return;
	}
	
    } else if (rb == 0) {
	throw FatalException("What just happened?");
    } else {
	if (msg.read(rb->get_buf(), rb->get_len(), roff) == 0) {
	    logger.warning("Invalid message");
	    close();
	    return;
	}
    }
    
    switch (state) {
    case HANDSHAKE: {
	if (msg.get_type() != VSRMessage::CONTROL) {
	    logger.warning(std::string("Invalid message sequence: state ") 
			   + to_string(uint64_t(state)) 
			   + " message " 
			   + to_string(uint64_t(msg.get_type())));
	    close();
	    return;
	}
	VSRCommand cmd = msg.get_command();
	if (cmd.get_type() != VSRCommand::SET) {
	    logger.warning("Invalid message sequence");
	    close();
	    return;
	}
	state = CONNECTED;
	VSRCommand respcmd(VSRCommand::RESULT, VSRCommand::SUCCESS);
	VSRMessage resp(respcmd);
	WriteBuf wb(0, 0);
	wb.prepend_hdr(resp.get_raw(), resp.get_raw_len());
	
	if (tp->handle_down(&wb, 0)) {
	    logger.error("Fixme!!!");
	    close();
	}
	break;
    }
    case CONNECTED: {
	if (msg.get_type() == VSRMessage::CONTROL) {
	    VSRCommand cmd = msg.get_command();
	    WriteBuf wb(0, 0);
	    if (cmd.get_type() == VSRCommand::JOIN || cmd.get_type() == VSRCommand::LEAVE) {
		logger.info(std::string("Cmd ") 
			    + to_string(uint64_t(cmd.get_type())) 
			    + " " +  cmd.get_address().to_string());
		try {
		    if (cmd.get_type() == VSRCommand::JOIN)
			vs->join(cmd.get_address().get_service_id());
		    else
			vs->leave(cmd.get_address().get_service_id());
		    VSRCommand response(VSRCommand::RESULT, VSRCommand::SUCCESS);
		    VSRMessage rmsg(response);
		    wb.prepend_hdr(rmsg.get_raw(), rmsg.get_raw_len());
		} catch (Exception e) {
		    logger.info(e.what());
		    VSRCommand response(VSRCommand::RESULT, VSRCommand::FAIL);
		    VSRMessage rmsg(response);
			
		    wb.prepend_hdr(rmsg.get_raw(), rmsg.get_raw_len());
		}
	    }
	    tp->handle_down(&wb, 0);
	} else if (msg.get_type() == VSRMessage::VSPROTO) {
	    logger.trace(std::string("VSPROTO: len = ") + 
			 ::to_string(rb->get_len(roff + msg.get_raw_len())));
	    WriteBuf wb(rb->get_buf(roff + msg.get_raw_len()), rb->get_len(roff + msg.get_raw_len()));
	    vs->handle_down(&wb, 0);
	    stats.incr_in(rb->get_len(roff + msg.get_raw_len()));
	} else {
	    logger.info("Invalid message");
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

void ClientHandler::handle_up(const int cid, const ReadBuf *rb, 
			      const size_t roff, 
			      const ProtoUpMeta *um)
{
    switch (cid) {
    case VS:
	handle_vs(rb, roff, um);
	break;
    case TP:
	handle_tp(rb, roff, um);
    }
}

void ClientHandler::start() 
{
    try {
	vs->connect("fifo");
	VSRMessage rmsg(vs->get_self());
	WriteBuf wb(0, 0);
	wb.prepend_hdr(rmsg.get_raw(),rmsg.get_raw_len());
	if (tp->handle_down(&wb, 0)) {
	    close();
	} else {
	    logger.info("Sent handshake");
	}
	return;
    } catch (Exception e) {
	logger.warning(std::string("Exception: ") + e.what());
	close();
	return;
    }
}


VSServer::VSServer(const char *a) : listener(0), terminate(false) 
{
    addr = ::strdup(a);
    tp_poll = Poll::create("def");
    fifo_poll = Poll::create("fifo");
}

VSServer::~VSServer()
{
    cleanup();
    free(addr);
    delete tp_poll;
    delete fifo_poll;
    delete listener;
}
    
void VSServer::handle_up(const int cid, const ReadBuf *rb, const size_t roff, 
			 const ProtoUpMeta *um)
{
    Transport *tp = listener->accept(tp_poll);
    tp->set_contention_params(1, 5);
    VSBackend *vs = VSBackend::create("fifo", fifo_poll);
    ClientHandler *cl = new ClientHandler(tp, vs);
    clients.push_back(cl);
    cl->start();
}

void VSServer::start() 
{
    listener = Transport::create(addr, tp_poll, this);
    listener->listen(addr);
}

void VSServer::stop() 
{
    listener->close();
    delete listener;
    terminate = true;
}

void VSServer::cleanup() 
{
    std::list<ClientHandler *>::iterator i, i_next;
    for (i = clients.begin(); i != clients.end(); i = i_next) {
	i_next = i, ++i_next;
	if ((*i)->get_state() == ClientHandler::CLOSED) {
	    delete *i;
	    clients.erase(i);
	}
    }
}

int VSServer::run() 
{
    int err;
    terminate = false;
    while ((err = tp_poll->poll(1000)) >= 0 && terminate == false) {
	while ((err = fifo_poll->poll(0)) > 0);
	if (err < 0 ) {
	    terminate = true;
	    break;
	}
	cleanup();
    }
    return err;
}



#ifdef COMPILE_SERVER

#include <csignal>
#include <string>

int main(int argc, char *argv[])
{

    logger.info("start");
    ::signal(SIGPIPE, SIG_IGN);

    if (argc < 2) {
	std::cerr << "Usage: " << argv[0] << " <address>" << std::endl;
	exit (-1);
    }
    try {
	std::string srv_arg("async");
	srv_arg += argv[1];
	VSServer s(srv_arg.c_str());
	s.start();
	s.run();
	s.stop();
    } catch (Exception e) {
	std::cerr << e.what() << "\n";
	return 1;
    }
    return 0;
}

#endif // COMPILE_SERVER
