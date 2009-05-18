#ifndef EVS_HPP
#define EVS_HPP

#include "gcomm/common.hpp"
#include "gcomm/transport.hpp"
#include "gcomm/uri.hpp"
#include "gcomm/uuid.hpp"

#include <map>
#include <set>


BEGIN_GCOMM_NAMESPACE

class EVSProto;

class EVS : public Transport
{
    Transport *tp;
    EVSProto *proto;
    Monitor mon;
public:
    
    EVS(const URI& uri_, EventLoop* event_loop_);
    ~EVS();

    void connect();
    void close();
    
    void handle_up(const int, const ReadBuf*, const size_t, const ProtoUpMeta*);
    int handle_down(WriteBuf*, const ProtoDownMeta*);
    
    bool supports_uuid() const;
    const UUID& get_uuid() const;

    size_t get_max_msg_size() const;

};

END_GCOMM_NAMESPACE

#endif // EVS_HPP
