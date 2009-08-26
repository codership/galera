
#include "vsbes.hpp"
#include "galeracomm/logger.hpp"

// Message and command definitions
#include "vs_remote_backend.hpp"
#include "galeracomm/vs.hpp"

#include <cstring>
#include <iostream>
#include <sstream>
#include <sys/time.h>



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
    Stats() :
        n_msgs_out(0),
        bytes_out(0),
        n_msgs_in(0),
        bytes_in(0),
        tlast(0)
    {
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
	    log_info << "Queued:    " << n_msgs_in << " msgs "
	                              << bytes_in  << " bytes";
	    log_info << "    "
		     << (1.e6*double(n_msgs_in)/(tnow - tlast)) << " msg/sec";
	    log_info << "    "
		     << (1.e6*double(bytes_in)/(tnow - tlast)) << " bytes/sec";
	    log_info << "Forwarded: " << n_msgs_out << " msgs " 
			              << bytes_out << " bytes ";
	    log_info << "    "
		     << (1.e6*double(n_msgs_out)/(tnow - tlast)) << " msg/sec";
	    log_info << "    " 
		     << (1.e6*double(bytes_out)/(tnow - tlast)) <<" bytes/sec";
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


ClientHandler::ClientHandler(Transport *t, VSBackend *v) :
    vs(v),
    tp(t),
    state(HANDSHAKE),
    flags(static_cast<Flags>(0))
{
    tp->set_up_context(this, TP);
    vs->set_up_context(this, VS);
}

ClientHandler::~ClientHandler()
{
    if (vs)
	log_info << "deleting " + vs->get_self().to_string();
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
	log_info << "Null message, silent drop";
    } else {
	size_t read_len = rb->get_len(roff);
	
	if (flags & F_DROP_OWN_DATA) {
	    VSMessage vmsg;

	    if (vmsg.read(rb->get_buf(), rb->get_len(), roff) == 0) {
		log_fatal << "Detected data corruption";
		throw FatalException("Data corruption");
	    }

	    if (vmsg.get_type()   == VSMessage::DATA &&
                vmsg.get_source() == vs->get_self()) {
		read_len = vmsg.get_hdrlen();
//		log_debug << "Dropping own data, hdr len: " << read_len;
	    }
	}
	
	WriteBuf wb(rb->get_buf(roff), read_len);
	VSRMessage msg;
	wb.prepend_hdr(msg.get_raw(), msg.get_raw_len());
	int err;
	if ((err = tp->handle_down(&wb, 0)) == EAGAIN) {
	    LOG_INFO("Fixme!!!");
	    close();
	    return;
	} else if (err) {
	    LOG_ERROR(std::string("Error: ") + strerror(err));
	    close();
	    return;
	}
	stats.incr_out(read_len);
    }
}
    
void ClientHandler::handle_tp(const ReadBuf *rb, const size_t roff,
			      const ProtoUpMeta *um)
{
    VSRMessage msg;
    if (rb == 0 && tp->get_state() == TRANSPORT_S_FAILED) {
	LOG_DEBUG("rb = 0 and tp state = failed"); 
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
		LOG_INFO("error: handle_down()");
		close();
	    }
	    return;
	} catch (std::exception& e) {
	    LOG_ERROR(std::string("Exception: ") + e.what());
	    close();
	    return;
	}
	
    } else if (rb == 0) {
	throw FatalException("What just happened?");
    } else {
	if (msg.read(rb->get_buf(), rb->get_len(), roff) == 0) {
	    LOG_WARN("Invalid message");
	    close();
	    return;
	}
    }
    
    switch (state) {
    case HANDSHAKE: {
	if (msg.get_type() != VSRMessage::CONTROL) {
	    log_warn << "Invalid message sequence: state: " << state
                     << ", message type: " << msg.get_type();
	    close();
	    return;
	}
	VSRCommand cmd = msg.get_command();
	if (cmd.get_type() != VSRCommand::SET) {
	    log_warn << "Invalid message sequence";
	    close();
	    return;
	}
	if (cmd.get_flags() & VSRCommand::F_DROP_OWN_DATA)
	    flags = F_DROP_OWN_DATA;
	state = CONNECTED;
	VSRCommand respcmd(VSRCommand::RESULT, VSRCommand::SUCCESS);
	VSRMessage resp(respcmd);
	WriteBuf wb(0, 0);
	wb.prepend_hdr(resp.get_raw(), resp.get_raw_len());
	
	if (tp->handle_down(&wb, 0)) {
	    LOG_ERROR("Fixme!!!");
	    close();
	}
	break;
    }
    case CONNECTED: {
	if (msg.get_type() == VSRMessage::CONTROL) {
	    VSRCommand cmd = msg.get_command();
	    WriteBuf wb(0, 0);
	    if (cmd.get_type() == VSRCommand::JOIN || cmd.get_type() == VSRCommand::LEAVE) {
		log_info << "Cmd: " << cmd.get_type()  << " from "
		         << cmd.get_address().to_string();
		try {
		    if (cmd.get_type() == VSRCommand::JOIN)
			vs->join(cmd.get_address().get_service_id());
		    else
			vs->leave(cmd.get_address().get_service_id());
		    VSRCommand response(VSRCommand::RESULT, VSRCommand::SUCCESS);
		    VSRMessage rmsg(response);
		    wb.prepend_hdr(rmsg.get_raw(), rmsg.get_raw_len());
		} catch (std::exception& e) {
		    LOG_INFO(e.what());
		    VSRCommand response(VSRCommand::RESULT, VSRCommand::FAIL);
		    VSRMessage rmsg(response);
			
		    wb.prepend_hdr(rmsg.get_raw(), rmsg.get_raw_len());
		}
	    }
	    tp->handle_down(&wb, 0);
	} else if (msg.get_type() == VSRMessage::VSPROTO) {
//	    log_debug << "VSPROTO: len = "
//	              << rb->get_len(roff + msg.get_raw_len());
	    // TODO: Validate
	    VSMessage vmsg;
	    if (vmsg.read(rb->get_buf(),
			  rb->get_len(), roff + msg.get_raw_len()) == 0) {
		LOG_WARN("Couldn't read protocol message");
		close();
		return;
	    }
	    WriteBuf wb(rb->get_buf(roff + msg.get_raw_len()), rb->get_len(roff + msg.get_raw_len()));
	    vs->handle_down(&wb, 0);
	    stats.incr_in(rb->get_len(roff + msg.get_raw_len()));
	} else {
	    LOG_WARN("Invalid message");
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
	    LOG_INFO("Sent handshake");
	}
	return;
    } catch (std::exception& e) {
	LOG_WARN(std::string("Exception: ") + e.what());
	close();
	return;
    }
}


VSServer::VSServer(const char *a) :
    clients   (),
    listener  (0),
    addr      (::strdup(a)),
    tp_poll   (Poll::create("def")),
    fifo_poll (Poll::create("fifo")),
    terminate (false)
{}

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
    tp->set_contention_params(1, 500);
    tp->set_max_pending_bytes(50*1024*1024);
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
	while ((err = fifo_poll->poll(0)) > 0) {}
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

extern "C" { static void (* const _SIG_IGN)(int) = SIG_IGN; }

int main(int argc, char *argv[])
{
    if (argc < 2) {
	std::cerr << "Usage: " << argv[0] << " <address>" << std::endl;
	exit (-1);
    }

    gu_conf_self_tstamp_on(); // turn on the timestamps
    
    log_info << "start";
    
    ::signal(SIGPIPE, _SIG_IGN);
    
    (::getenv("VSBES_DEBUG")) ? gu_conf_debug_on() : gu_conf_debug_off();

    try {
	std::string srv_arg("async");
	srv_arg += argv[1];
	VSServer s(srv_arg.c_str());
	s.start();
	s.run();
	s.stop();
    } catch (std::exception& e) {
	log_error << e.what();
	return 1;
    }
    return 0;
}

#endif // COMPILE_SERVER
