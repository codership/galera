
#include "gcomm/transport.hpp"


BEGIN_GCOMM_NAMESPACE

class EVSProto;
class PCProto;


class PC : public Transport
{
    Transport* tp; // GMCast transport
    EVSProto* evs; // EVS protocol layer
    PCProto* pc;   // PC protocol layer

    PC(const PC&);
    void operator=(const PC&);
public:
    PC(const URI&, EventLoop*, Monitor*);
    ~PC();
     
    void connect();
    void close();

    void handle_up(const int, const ReadBuf*, const size_t, const ProtoUpMeta*);
    int handle_down(WriteBuf*, const ProtoDownMeta*);

    bool supports_uuid() const;
    const UUID& get_uuid() const;
    
    size_t get_max_msg_size() const;

};

END_GCOMM_NAMESPACE
