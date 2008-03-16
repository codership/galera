
#include <gcomm/transport.hpp>
#include <gcomm/exception.hpp>


#include <cstdlib>
#include <sys/time.h>
#include <sched.h>
#include <ctime>
#include <csignal>
#include <cerrno>
#include <vector>
#include <list>

class Timeval {
public:
    uint64_t tstamp;
    Timeval() {
	timeval tv;
	::gettimeofday(&tv, 0);
	tstamp = tv.tv_sec;
	tstamp *= 1000000;
	tstamp += tv.tv_usec;
    }
    Timeval(const uint64_t t) : tstamp(t) {}
    Timeval(const timeval& tv) {
	tstamp = tv.tv_sec;
	tstamp *= 1000000;
	tstamp += tv.tv_usec;
    }
    Timeval(const unsigned char *buf, const size_t buflen) {
	if (read_uint64(buf, buflen, 0, &tstamp) == 0)
	    throw DException("");
    }
};

class Message {
    Timeval tstamp;
    unsigned char *raw;
    size_t size;
public:
    Message(const size_t s) : size(s + 8) {
	raw = new unsigned char[size];
	if (write_uint64(tstamp.tstamp, raw, 8, 0) == 0)
	    throw DException("");

	::memset(&raw[8], 0xab, size - 8); 
    }
    Message(const ReadBuf *rb, const size_t off) {
	size = rb->get_len(off);
	if (size < 8)
	    throw DException("");
	raw = new unsigned char[size];
	::memcpy(raw, rb->get_buf(off), rb->get_len(off));
	tstamp = Timeval(raw, 8);
    }
    ~Message() {
	delete[] raw;
    }
    uint64_t get_period() const {
	timeval tv;
	::gettimeofday(&tv, 0);
	Timeval tnow(tv);

	return tnow.tstamp - tstamp.tstamp;
    }
    const unsigned char *get_raw() const {
	return raw;
    }
    size_t get_raw_len() const {
	return size;
    }
};


class Client : public Toplay {
    Transport *tp;
    Poll *poll;
    unsigned long long sent;
    unsigned long long recvd;
    Timeval first_sent;
    Timeval last_recvd;
    unsigned long long bytes;
    std::vector<uint64_t> periods;
    uint64_t max_period;
    uint64_t min_period;
    bool terminated;
public:
    Client(Poll *p) : tp(0), poll(p), 
		      sent(0), recvd(0),
		      first_sent(0),
		      last_recvd(1),
		      bytes(0),
		      max_period(0),
		      min_period((uint64_t)-1), 
		      terminated(false) {}
    ~Client() {

	
    }
    void print_stats() {
	double p = 0.;
	for (std::vector<uint64_t>::iterator i = periods.begin();
	     i != periods.end(); ++i) {
	    // std::cout << *i << "\n";
	    p += *i;
	}
	std::cerr << "Running time: " << last_recvd.tstamp - first_sent.tstamp << " usec\n";
	std::cerr << "Samples: " << periods.size() << "\n";
	std::cerr << "Msg rate: " << (periods.size()*1.e6/(last_recvd.tstamp - first_sent.tstamp)) << " msg/sec\n";
	std::cerr << "Min latency: " << min_period << " usec\n";
	std::cerr << "Max latency: " << max_period << " usec\n";
	std::cerr << "Average latency : " << (p/periods.size()) << " usec\n";
	std::cerr << "Throughput: " << (1.e6*double(bytes)/(last_recvd.tstamp - first_sent.tstamp)) << " bytes/sec\n";
	bytes = 0;
	first_sent.tstamp = 0;
	last_recvd.tstamp = 1;
	periods.clear();
    }
    
    void connect(const char *addr) {
	tp = Transport::create(addr, poll, this);
	tp->connect(addr);
    }
    void close() {
	tp->close();
	delete tp;
	tp = 0;
    }

    void handle_up(const int cid, const ReadBuf *rb, const size_t off, const ProtoUpMeta *um) {
	if (rb == 0 && tp->get_state() == TRANSPORT_S_CONNECTED)
	    return;
	else if (rb == 0) {
	    if (tp->get_state() == TRANSPORT_S_FAILED)
		throw DException("");
	    else 
		return; // Closed?
	}
	recvd++;
	Message msg(rb, off);
	periods.push_back(msg.get_period());
	max_period = std::max(max_period, msg.get_period());
	min_period = std::min(min_period, msg.get_period());
	bytes += rb->get_len(off);
	
	timeval tv;
	::gettimeofday(&tv, 0);
	last_recvd = Timeval(tv);
	// std::cerr << msg.get_period() << "\n";
    }
    
    int send(const size_t nbytes) {
	Message msg(nbytes);
	WriteBuf *wb = new WriteBuf(msg.get_raw(), msg.get_raw_len());
	int ret;
	if ((ret = pass_down(wb, 0)) == EAGAIN) {
	    std::cerr << "EAGAIN\n";
	    delete wb;
	    return EAGAIN;
	} 

	if (ret == 0) {
	    if (first_sent.tstamp == 0) {
		timeval tv;
		::gettimeofday(&tv, 0);
		first_sent = Timeval(tv);
	    }
	    sent++;
	}
	delete wb;
	return ret;
    }
    
    int handle_down(WriteBuf *wb, const ProtoDownMeta *) {
	throw DException("");
	return 0;
    }
    unsigned long long unreceived() const {
	return sent - recvd;
    }
    bool is_connected() const {
	return tp->get_state() == TRANSPORT_S_CONNECTED;
    }
};


class ClientHandler : public Toplay {
    Transport *tp;
    unsigned long long handled;
public:
    bool terminated;
    ClientHandler() : tp(0), handled(0), terminated(false) {}
    ~ClientHandler() {
	if (tp)
	    tp->close();
        delete tp;
	std::cerr << "Exiting client handler, Handled: " << handled << " messages\n";
    }
    void set_transport(Transport *t) {tp = t;}

    void handle_up(int cid, const ReadBuf *rb, const size_t off, const ProtoUpMeta *um) {
	if (rb == 0 || terminated) {
	    terminated = true;
	    return;
	}
	WriteBuf *wb = new WriteBuf(rb->get_buf(off), rb->get_len(off));
	if (pass_down(wb, 0) == EAGAIN) {
	    terminated = true;
	} else {
	    handled++;
	}
	delete wb;
    }
};

class Server : public Toplay {
    Transport *tp;
    Poll *poll;
    std::list<ClientHandler *> clients;
public:
    Server(Poll *p) : tp(0), poll(p) {}
    ~Server() {
	cleanup();
	delete tp;
    }

    void cleanup() {
	for (std::list<ClientHandler *>::iterator i = clients.begin();
	     clients.size() > 0 && i != clients.end();) {
	    if ((*i)->terminated) {
		delete *i;
		clients.erase(i);
		i = clients.begin();
	    } else {
		++i;
	    }
	}
    }


    void listen(const char *addr) {
	tp = Transport::create(addr, poll, this);
	tp->listen(addr);
    }
    
    void handle_up(const int cid, const ReadBuf *rb, const size_t off, const ProtoUpMeta *um) {
	ClientHandler *ch = new ClientHandler();
	Transport *clitp = tp->accept(poll, ch);
	ch->set_transport(clitp);
	clients.push_back(ch);
    }
};


static bool terminated = false;
void sigint(int signo)
{
    terminated = true;
}

int main(int argc, char *argv[])
{
    bool is_realtime = false;
    Poll *p = Poll::create("Def");

    if (argc < 3) {
	std::cerr << "Usage: " << argv[0] << " <--client|--server> <addr>\n";
	return EXIT_FAILURE;
    }
    
    ::signal(SIGPIPE, SIG_IGN);
    ::signal(SIGINT, &sigint);
    if (::getenv("PERF_REALTIME")) {
	sched_param sp;
	::memset(&sp, 0, sizeof(sp));
	sp.sched_priority = 50;

	if (::sched_setscheduler(getpid(), SCHED_FIFO, &sp)) {
	    std::cerr << "Could not set scheduler: " << strerror(errno) << "\n";
	} else {
	    is_realtime = true;
	}
    }

    try {
	if (strcmp(argv[1], "--client") == 0) {
	    Client c(p);
	    c.connect(argv[2]);
	    unsigned long long msgs = 1000;
	    unsigned long long nbytes = 1000;
	    if (argc > 3)
		msgs = ::strtoull(argv[3], 0, 0);
	    if (argc > 4)
		nbytes = ::strtoull(argv[4], 0, 0);
	    while (c.is_connected() == false && p->poll(1000) >= 0);
	    for (unsigned long long i = 0; i < msgs && terminated == false;) {
		int ret;
		if ((ret = c.send((rand()%nbytes)*2)) == 0)
		    i++;
		else if (ret != EAGAIN)
		    throw DException(strerror(ret));
		p->poll(c.unreceived() < 16 ? 0 : 10);
		if (i % 10000 == 0) {
		    std::cerr << "Progress: " << (i*100/msgs) << "%\n";
		    c.print_stats();
		}
	    }
	    while (c.unreceived() && terminated == false)
		p->poll(1000);
	    c.print_stats();
	    c.close();
	} else if (strcmp(argv[1], "--server") == 0) {
	    Server s(p);
	    s.listen(argv[2]);
	    int ret;
	    unsigned long long cnt = 0;
	    clock_t cstart = clock();
	    time_t tstart = time(0);
	    while ((ret = p->poll(1000)) >= 0 && terminated == false) {
		s.cleanup();

		if (is_realtime) {
		    if (ret)
			cnt++;
		    else {
			cnt = 0;
			cstart = clock();
			tstart = time(0);
		    }
		    if (cnt > CLOCKS_PER_SEC) {
			clock_t cstop = clock();
			time_t tstop = time(0);
			if ((cstop - cstart)/(9*CLOCKS_PER_SEC) > (tstop - tstart)/10) {
			    std::cerr << "Looping too fast, back to normal scheduling\n";
			    sched_param sp;
			    ::memset(&sp, 0, sizeof(sp));
			    sp.sched_priority = 0;
			    ::sched_setscheduler(getpid(), SCHED_OTHER, &sp);
			    is_realtime = false;
			}
		    }
		}
	    }
	} else {
	    return EXIT_FAILURE;
	}
    }
    catch (Exception e) {
	std::cerr << e.what() << "\n";
	return EXIT_FAILURE;
    }
    
    delete p;
    return EXIT_SUCCESS;
}
