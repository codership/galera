#ifndef EVS_HPP
#define EVS_HPP

#include "galeracomm/transport.hpp"
#include "galeracomm/address.hpp"

#include <map>
#include <set>

class EVSProto;



class EVS : public Protolay {

    Transport *tp;
    EVSProto *proto;

    EVS (const EVS&);
    EVS& operator= (const EVS&);

    EVS () : tp(0), proto(0) {}

public:

    void connect(const char *addr);
    void close();
    
    void join(const ServiceId, Protolay*);
    void leave(const ServiceId);
    
    void handle_up(const int, const ReadBuf*, const size_t, const ProtoUpMeta *);
    int handle_down(WriteBuf*, const ProtoDownMeta*);
    
    static EVS* create(const char *, Poll *p);
};

#endif // EVS_HPP
