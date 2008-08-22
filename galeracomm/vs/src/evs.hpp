#ifndef EVS_HPP
#define EVS_HPP

#include "gcomm/transport.hpp"

#include <map>
#include <set>

class EVSProto;

struct EVSProtoUpMeta {
    const Sockaddr& source;
    EVSProtoUpMeta(const Sockaddr& source_) : source(source_) {}
};

class EVS : public Protolay {
    Transport *tp;
    EVSProto *proto;
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
