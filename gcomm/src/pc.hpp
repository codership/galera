
#include "gcomm/transport.hpp"
#include "gcomm/pseudofd.hpp"

namespace gcomm
{
    class PCProto;
    namespace evs
    {
        class Proto;
    } // namespace gcomm

    class PC : public Transport, public EventContext
    {
        PseudoFd    pfd;                // PseudoFd for timer handler
        Transport*  tp;                 // GMCast transport
        evs::Proto* evs;                // EVS protocol layer
        PCProto*    pc;                 // PC protocol layer
        Period      leave_grace_period; // Period to wait graceful leave
        
        PC(const PC&);
        void operator=(const PC&);
        
    public:
        
        PC (const URI&, EventLoop*, Monitor*);
        
        ~PC();
        
        void connect();
        void close();
        
        void handle_up(int, const ReadBuf*, size_t, const ProtoUpMeta&);
        int  handle_down(WriteBuf*, const ProtoDownMeta&);

        void handle_event(int, const Event&);
        
        bool supports_uuid() const;
        const UUID& get_uuid() const;
        
        size_t get_max_msg_size() const;
    };
    
} // namespace gcomm
