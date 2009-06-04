
#include "gcomm/transport.hpp"

BEGIN_GCOMM_NAMESPACE

class PC : public Transport
{
public:
    PC(const URI&, EventLoop*, Monitor*);
    ~PC();

    void connect();
    void close();

    void handle_up(const int, const ReadBuf*, const size_t, const ProtoUpMeta*);
    int handle_down(WriteBuf*, const ProtoDownMeta*);

    bool support_uuid() const;
    const UUID& get_uuid() const;
    
    size_t get_max_msg_size() const;

};

END_GCOMM_NAMESPACE
