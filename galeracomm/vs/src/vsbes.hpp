#ifndef VSBES_HPP
#define VSBES_HPP

#include "galeracomm/transport.hpp"
#include "vs_backend.hpp"

#include <list>

class ClientHandler : public Toplay {
    VSBackend *vs;
    Transport *tp;

    ClientHandler (const ClientHandler&);
    ClientHandler& operator= (const ClientHandler&);

public:

    enum State {CLOSED, HANDSHAKE, CONNECTED} state;
    enum {
	TP, VS
    };
    enum Flags {
	F_DROP_OWN_DATA = 0x1
    } flags;
    ClientHandler(Transport*, VSBackend*);
    ~ClientHandler();
    State get_state() const;
    void close();
    void handle_vs(const ReadBuf*, const size_t, const ProtoUpMeta*);
    void handle_tp(const ReadBuf*, const size_t, const ProtoUpMeta*);
    void handle_up(const int cid, const ReadBuf*, const size_t, 
		   const ProtoUpMeta*);
    void start();
};

class VSServer : public Toplay {
    std::list<ClientHandler *> clients;
    Transport *listener;
    char *addr;
    Poll *tp_poll;
    Poll *fifo_poll;
    bool terminate;
    void cleanup();

    VSServer (const VSServer&);
    VSServer& operator= (const VSServer&);
public:
    VSServer(const char *a);
    ~VSServer();
    void start();
    void stop();
    int run();
    void handle_up(const int cid, const ReadBuf *rb, const size_t roff, 
		   const ProtoUpMeta *um);
};

#endif // VSBES_HPP
