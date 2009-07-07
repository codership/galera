#ifndef VS_HPP
#define VS_HPP

#include "gcomm/common.hpp"
#include "gcomm/uri.hpp"
#include "gcomm/transport.hpp"
#include "gcomm/view.hpp"

BEGIN_GCOMM_NAMESPACE

class VSMessage;
class EVSProto;
class VSProto;
class Poll;
class EVS;

class VS : public Transport
{
    Transport* tp;
    EVSProto* evs_proto;
    VSProto* proto;
    VS(const VS&);
    void operator=(const VS&);
public:
    void connect();
    void close();
    
    int handle_down(WriteBuf*, const ProtoDownMeta*);
    void handle_up(const int, const ReadBuf*, const size_t, const ProtoUpMeta*);
    
    size_t get_max_msg_size() const;

    bool supports_uuid() const;
    const UUID& get_uuid() const;

    VS(const URI&, EventLoop*, Monitor*);
    ~VS();
};

END_GCOMM_NAMESPACE

#endif // VS_HPP
