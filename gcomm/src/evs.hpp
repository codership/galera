#ifndef EVS_HPP
#define EVS_HPP

#include "gcomm/common.hpp"
#include "gcomm/transport.hpp"
#include "gcomm/uri.hpp"
#include "gcomm/uuid.hpp"

#include <map>
#include <set>



namespace gcomm
{
    namespace evs
    {
        class Proto;
    } // namespace evs

    class EVS : public Transport
    {
        Transport *tp;
        gcomm::evs::Proto  *proto;
        
        EVS(const EVS&);
        void operator=(const EVS&);

        Period join_wait_period;
        
    public:
        
        EVS(const URI& uri_, EventLoop* event_loop_, Monitor*);
        ~EVS();
        
        void connect();
        void close();
        
        void handle_up(int, const ReadBuf*, size_t, 
                       const ProtoUpMeta&);
        int  handle_down(WriteBuf*, const ProtoDownMeta&);
        
        bool supports_uuid() const;
        const UUID& get_uuid() const;
        
        size_t get_max_msg_size() const;
        
        gcomm::evs::Proto* get_proto() const
        {
            return proto;
        }
    };
} // namespace gcomm


#endif // EVS_HPP
