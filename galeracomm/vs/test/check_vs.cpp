extern "C" {
#include "galeracomm/vs.h"
#include "vs_backend.h"
#include "vs_backend_shm.h"
}

#include <errno.h>
#include <set>
#include <string>
#include <exception>
#include <vector>
#include <iostream>
#include <cassert>

std::string to_string(const vs_msg_e e)
{
     switch (e) {
     case VS_MSG_TRANS_CONF:
	  return "TRANS_CONF";
     case VS_MSG_REG_CONF:
	  return "REG_CONF";
     case VS_MSG_DATA:
	  return "DATA";
     case VS_MSG_ERR:
	  return "ERR";
     }
}

typedef uint64_t EventId;

class Exception : public std::exception {
     std::string reason;
public:
     Exception(const char *str) : reason(str) {}
     ~Exception() throw() {}
     const char *what() const throw() {
	  return reason.c_str();
     }
};

class Time {
     uint64_t t;
     static Time wall_clock;

public:
     enum {USEC = 1ULL, MSEC = 1000ULL*USEC, SEC = 1000ULL*MSEC};
     Time(uint64_t t_) : t(t_) {}

     static Time get_time();
     static void set_time(const Time t);
     bool operator<(const Time &b) const {
	  return t < b.to_int();
     }
     Time operator+(const Time &b) const {
	  return t + b.to_int();
     }

     std::string to_string() const {
	  char buf[64];
	  snprintf(buf, sizeof(buf), "%u.%6.6u", 
		   (uint32_t)(t/SEC), (uint32_t)(t%SEC));
	  return buf;
     }
     uint64_t to_int() const {return t;}
};

bool operator==(const Time &a, const Time &b)
{
     return a.to_int() == b.to_int();
}

Time operator-(const Time &a, const Time &b) throw()
{
     if (a < b)
	  throw Exception("Tried to substract time from smaller");
     return a.to_int() - b.to_int();
}
     
Time Time::wall_clock(0);
Time Time::get_time() {
     return wall_clock;
}

void Time::set_time(const Time t)
{
     if (t < wall_clock)
	  throw Exception(("Uncausal operation: " + t.to_string() + " < " + get_time().to_string()).c_str());
     wall_clock = t;
}

Time max(const Time &a, const Time &b)
{
     return a < b ? b : a;
}



class EventHandler;
class EventData {
     void *data;
     const EventHandler *source;
public:
     EventData(void *d, const EventHandler *src) : data(d), source(src) {}
     void *get_data() {return data;}
     const EventHandler *get_source() const {return source;}
};

class EventHandler {
public:
     virtual ~EventHandler() {}
     virtual void on_event(EventData*) = 0;
};


class Event {
     EventId id;
     Time    at;
     EventHandler *eh;
     EventData    *edata;
     void schedule();
public:
     static Event *get_event() {
	  Event *ev = 0;
	  if (events.empty() == false) {
	       ev = *events.begin();
	       events.erase(events.begin());
	  }
	  return ev;
     }

     Event(const Time after, EventHandler *h, EventData *d) : 
	  at(after + Time::get_time()), eh(h), edata(d) {
	  id = last_id++;
	  schedule();
     }
     ~Event() {}


     EventId get_id() const {
	  return id;
     }

     Time get_at() const {
	  return at;
     }
     
     void handle() {
	  eh->on_event(edata);
     }
     struct PtrLessThan {
	  bool operator()(const Event *a, const Event *b) const {
	       if (a->get_at() == b->get_at())
		    return a->get_id() < b->get_id();
	       else 
		    return a->get_at() < b->get_at();
	  }
     };     
private:

     static EventId last_id;
     static std::multiset<Event *, Event::PtrLessThan> events;
};



EventId Event::last_id = 0;
std::multiset<Event *, Event::PtrLessThan> Event::events;

std::string to_string(const uint64_t &v)
{
     char buf[64];
     snprintf(buf, sizeof(buf), "%llu", v);
     return buf;     
}

std::string to_string(const int &v)
{
     char buf[11];
     snprintf(buf, sizeof(buf), "%i", v);
     return buf;     
}

void print_event(const Event *e)
{
     std::cout << "Event: " + to_string(e->get_id()) + " at " + 
	  e->get_at().to_string() + "\n";
}

void Event::schedule() {
     events.insert(this);
}

/**
 * Vs server engine, implements backend... singleton
 */

class VsServer : public EventHandler {
     vs_backend_t *be;
     static VsServer *server;
     static void send_cb(EventHandler *, const vs_msg_t *);
     enum EType {SCHED, SPLIT, MERGE};
     class VsServerEvent : public EventData {
	  EType type;
     public:
	  VsServerEvent(EType t) : EventData(this, 0), type(t) {}
	  EType get_type() const {return type;}
     };
     Time split_interval;
     Time merge_interval;
public:
     VsServer() :
	  split_interval(Time::SEC*40), 
	  merge_interval(Time::SEC*20) {}
     static void start() throw() ;
     static void stop();
     void on_event(EventData*);
};

void VsServer::on_event(EventData *d)
{
     VsServerEvent *e = static_cast<VsServerEvent *>(d);
     switch (e->get_type()) {
     case SCHED:
	  vs_backend_sched(be, NULL, -1);
	  break;
     case SPLIT:
	  vs_backend_shm_split();
	  new Event(rand()%split_interval.to_int(), 
		    server, new VsServerEvent(SPLIT));
	  break;
     case MERGE:
	  vs_backend_shm_merge();
	  new Event(rand()%merge_interval.to_int(), 
		    server, new VsServerEvent(MERGE));
	  break;
     }
     delete d;
}

void VsServer::send_cb(EventHandler *ctx, const vs_msg_t *msg)
{
     new Event(100*Time::USEC + rand()%(3*Time::USEC), ctx, new VsServerEvent(SCHED));
}

void VsServer::start() throw()
{
     if (server) 
	  throw Exception("Tried to start VS server twice");
     server = new VsServer();
     server->be = vs_backend_new(
	  "shm:test", server, 
	  reinterpret_cast<vs_backend_cb_f>(&send_cb));
     if (!server->be)
	  throw Exception("Could not open vs backend");
     new Event(rand()%server->split_interval.to_int(), 
	       server, new VsServerEvent(SPLIT));
     new Event(rand()%server->merge_interval.to_int(), 
	       server, new VsServerEvent(MERGE));
}

void VsServer::stop()
{
     if (server) {
	  vs_backend_free(server->be);
	  delete server;
	  server = 0;
     }
}

VsServer (*VsServer::server)(0);


/**
 * Vs client 
 */


class VsClient : public EventHandler {
     vs_t *vs;
     std::string url;
     static void recv_cb(EventHandler *, const vs_msg_t *);
     EventHandler *msg_handler;
     Time last_recv_ev_at; 
public:
     VsClient(const char *ur, EventHandler *eh) : 
	  vs(0), url(ur), msg_handler(eh),
	  last_recv_ev_at(0) {}
     ~VsClient() {
	  vs_free(vs);
     }
     
     int open(const group_id_t) throw();
     void close() throw();
     
     int send(msg_t *, const vs_msg_safety_e) throw(); 
     void on_event(EventData*);
};


int VsClient::open(const group_id_t g) throw()
{
     int ret;
     if (vs)
	  throw Exception("Client already opened");
     if (!(vs = vs_new(url.c_str())))
	  throw Exception("Fatal error");
     vs_set_recv_cb(vs, this, reinterpret_cast<vs_recv_f>(&recv_cb));
     ret = vs_open(vs, g);
     if (ret)
	  vs_free(vs);
     return ret;
}

void VsClient::close() throw() {
     if (!vs)
	  throw Exception("Client not opened yet");
     vs_close(vs);
     vs_free(vs);
}

int VsClient::send(msg_t *msg, const vs_msg_safety_e s) throw()
{
     if (!vs)
	  throw Exception("Attempt to send on unopened client");
     return vs_send(vs, msg, s);
}

void VsClient::recv_cb(EventHandler *ctx, const vs_msg_t *msg)
{
     Event *ev;
     VsClient *vsc = static_cast<VsClient*>(ctx);
     Time after(rand()%10*Time::USEC +
		(vsc->last_recv_ev_at < Time::get_time() ? 0 : 
		 (vsc->last_recv_ev_at - Time::get_time()).to_int()));
     ev = new Event(after, ctx, 
		    new EventData(vs_msg_copy(msg), ctx));
     vsc->last_recv_ev_at = max(ev->get_at(), vsc->last_recv_ev_at);
}

void VsClient::on_event(EventData *d)
{
     vs_msg_t *msg;
     if (!(msg = reinterpret_cast<vs_msg_t*>(d->get_data())))
	  throw Exception("Got null message in vs client event handler");
     new Event(10*Time::USEC, msg_handler, d);
}


class Actor : EventHandler {
     enum State {CLOSED, OPENING, OPEN, CLOSING};
     std::string to_string(const State &s) {
	  switch (state) {
	  case CLOSED:
	       return "CLOSED";
	       break;
	  case OPENING:
	       return "OPENING";
	       break;
	  case OPEN:
	       return "OPEN";
	       break;
	  case CLOSING:
	       return "CLOSING";
	       break;
	  }
     }
     VsClient *vsc;
     State state;
     uint64_t messages;
     uint64_t sent;
     uint64_t delivered;
     uint64_t failed;
public:
     Actor() : vsc(0), state(CLOSED), messages(0),
	       sent(0), delivered(0), failed(0) {}
     void activate();
     void on_event(EventData*);
     uint64_t get_sent() {return sent;}
     uint64_t get_delivered() {return delivered;}
     uint64_t get_failed() {return failed;}
};

void Actor::activate()
{
     Time t((rand()%3000)*Time::MSEC);
     new Event(t, this, 0);
     messages = 33333333 + rand()%666;
}

void Actor::on_event(EventData *d)
{

     if (state == CLOSED) {
	  vsc = new VsClient("shm:test", this);
	  vsc->open(0);
	  state = OPENING;
     } else if (state == OPENING) {
	  vs_msg_t *msg = reinterpret_cast<vs_msg_t*>(d->get_data());
	  if (!msg)
	       throw Exception("wtf?");
	  if (vs_msg_get_type(msg) == VS_MSG_DATA) {
	       assert(0);
	       throw Exception("More wtf?");
	  }
	  if (vs_msg_get_type(msg) == VS_MSG_ERR)
	       throw Exception("Protocol error");
	  std::cout << "Msg type: " + ::to_string(vs_msg_get_type(msg)) + "\n";
	  if (vs_msg_get_type(msg) == VS_MSG_TRANS_CONF) {
	       std::cout << "About to be connected\n";
	  }
	  if (vs_msg_get_type(msg) == VS_MSG_REG_CONF) {
	       state = OPEN;
	       new Event((rand()%2)*Time::MSEC, this, 0);
	  }
	  vs_msg_free(msg);
     } else if (state == OPEN) {
	  if (!d && messages > 0) {
	       msg_t *msg = msg_new();
	       size_t payload_len = 32 + std::rand()%128;
	       char *payload = new char[payload_len];
	       msg_set_payload(msg, payload, payload_len);
	       int err = vsc->send(msg, VS_MSG_SAFETY_SAFE);
	       if (err && err != EAGAIN)
		    throw Exception(("Send failed: " + ::to_string(err)).c_str());
	       if (err == 0) {
		    messages--;
		    sent++;
	       } else {
		    failed++;
	       }
	       msg_free(msg);
	       delete[] payload;
	       if (sent%10000 == 0)
		    printf("Sent %llu messages\n", sent);
	       new Event((rand()%100)*Time::USEC, this, 0);
	  } else if (!d) {
	       vsc->close();
	       state = CLOSING;
	  } else {
	       // Handle message
	       delivered++;
	       vs_msg_free(reinterpret_cast<vs_msg_t*>(d->get_data()));
	  }
     } else if (state == CLOSING) {
	  vs_msg_t *msg = reinterpret_cast<vs_msg_t*>(d->get_data());
	  if (!msg)
	       throw Exception("wtf?");
	  if (vs_msg_get_type(msg) == VS_MSG_REG_CONF) {
	       std::cout << "About to be connected\n";
	  }
	  if (vs_msg_get_type(msg) == VS_MSG_TRANS_CONF) {
	       std::cout << "Closed\n";
	       delete vsc;
	       vsc = 0;
	       state = CLOSED;
	  }	  
     }
     delete d;
}

void main_loop()
{
     Event *e;
     Time last_t = Time::get_time();
     Time runtime(Time::SEC*20ULL);
     while ((e = Event::get_event()) && e->get_at() < runtime) {
	  Time::set_time(e->get_at());
	  e->handle();
	  delete e;
	  if (last_t + Time::SEC*10 < Time::get_time()) {
	       last_t = Time::get_time();
	       std::cout << "Wall clock " << last_t.to_string() << "\n";
	  }
     }
}

void activate(Actor& a)
{
     a.activate();
}

int main()
{
     std::vector<Actor> actors;
     VsServer::start();
     
     actors.resize(8);
     for_each(actors.begin(), actors.end(), activate);
     try {
	  main_loop();
     } catch (Exception e) {
	  std::cerr << "Error: " << e.what() << "\n";
     }
     VsServer::stop();

     uint64_t tot_sent = 0;
     uint64_t tot_delivered = 0;
     uint64_t tot_failed = 0;

     for (std::vector<Actor>::iterator i = actors.begin(); 
	  i != actors.end(); i++) {
	  tot_sent += i->get_sent();
	  tot_delivered += i->get_delivered();
	  tot_failed += i->get_failed();
     }
     
     std::cout << "tot sent: " << to_string(tot_sent) << " ";
     std::cout << "tot delivered: " << to_string(tot_delivered) << "\n";
     std::cout << "tot failed: " << to_string(tot_failed) << "\n";

     std::cout << "tot time: " << Time::get_time().to_string() << "\n";

     return EXIT_SUCCESS; 
}
