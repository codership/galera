#ifndef EVS_VS_MESSAGE_HPP
#define EVS_VS_MESSAGE_HPP

#include "gcomm/types.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/view.hpp"
#include "evs_seqno.hpp"

BEGIN_GCOMM_NAMESPACE

class VSMessage
{
public:
    enum Type {T_NONE, T_STATE, T_DATA, T_LEAVE, T_MAX};
private:
    int version;
    Type type;
    uint8_t user_type;
    ViewId source_view;
    uint32_t seq;
    View* view;

    // Metadata mainained only locally 
    UUID source;
protected:    

    VSMessage(const int version_,
              const Type type_,
              const uint8_t user_type_,
              const ViewId& source_view_,
              const uint32_t seq_,
              View* view_) :
        version(version_),
        type(type_),
        user_type(user_type_),
        source_view(source_view_),
        seq(seq_),
        view(view_)
    {
        
    }

public:    

    string to_string() const
    {
        return "vsmsg("
            + make_int(version).to_string() + ","
            + make_int(type).to_string() + ","
            + Int(user_type).to_string() + ","
            + source.to_string() + ","
            + source_view.to_string() + ","
            + make_int(seq).to_string() + ","
            + (view ? view->to_string() : ")");
    }

    VSMessage() : 
        version(0),
        type(T_MAX),
        user_type(255),
        source_view(ViewId()),
        seq(SEQNO_MAX),
        view(0) 
    {
        
    }

    VSMessage(const VSMessage& msg)
    {
        *this = msg;
        if (msg.get_view())
        {
            view = new View(*msg.get_view());
        }
    }

    ~VSMessage()
    {
        delete view;
    }
    
    Type get_type() const
    {
        return type;
    }
    
    uint8_t get_user_type() const
    {
        return user_type;
    }
    
    const ViewId& get_source_view() const
    {
        return source_view;
    }

    const View* get_view() const
    {
        return view;
    }
    
    uint32_t get_seq() const
    {
        return seq;
    }


    void set_source(const UUID& pid)
    {
        source = pid;
    }
    
    const UUID& get_source() const
    {
        return source;
    }

    // Serialization
    
    size_t read(const byte_t* buf, const size_t buflen, const size_t offset)
    {
        size_t off;
        uint32_t w;

        /* 0 -> 3 */
        if ((off = gcomm::read(buf, buflen, offset, &w)) == 0)
        {
            LOG_WARN("read vsmessage: read hdr");
            return 0;
        }
        version = w & 0xff;
        type = static_cast<Type>((w >> 8) & 0xff);
        user_type = (w >> 16) & 0xff;
        
        if (version != 0)
        {
            LOG_WARN("read vsmessage: invalid version: " + Int(version).to_string());
            return 0;
        }
        if (type <= T_NONE || type >= T_MAX)
        {
            LOG_WARN("read vsmessage: invalid type: " + Int(type).to_string());
            return 0;
        }
        
        /* 4 -> 7 */
        if ((off = gcomm::read(buf, buflen, off, &seq)) == 0)
        {
            LOG_WARN("read vsmessage: read seq");
            return 0;
        }

        if ((off = source_view.read(buf, buflen, off)) == 0)
        {
            LOG_WARN("read vsmessage: read source view");
            return 0;
        }

        if (type == T_STATE)
        {
            view = new View();
            if ((off = view->read(buf, buflen, off)) == 0)
            {
                LOG_WARN("read vsmessage: read view");
                return 0;
            }
        }
        return off;
    }

    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const
    {
        size_t off;
        uint32_t w;
        w = (version & 0xff) | ((type & 0xff) << 8) | ((user_type & 0xff) << 16); 
        if ((off = gcomm::write(w, buf, buflen, offset)) == 0)
        {
            LOG_WARN("write vsmessage: write hdr");
            return 0;
        }
        if ((off = gcomm::write(seq, buf, buflen, off)) == 0)
        {
            LOG_WARN("write vsmessage: write seq");
            return 0;
        }
        if ((off = source_view.write(buf, buflen, off)) == 0)
        {
            LOG_WARN("write vsmessage: write source view");
            return 0;
        }
        if (type == T_STATE)
        {
            if ((off = view->write(buf, buflen, off)) == 0)
            {
                LOG_WARN("write vsmessage: write view");
                return 0;
            }
        }
        return off;
    }

    size_t size() const
    {
        return 4 + 4 + source_view.size() + (view != 0 ? view->size() : 0);
    }

};

struct VSStateMessage : VSMessage
{
    VSStateMessage(const ViewId& source_view,
                   const View& view,
                   uint32_t next_seq) :
        VSMessage(0,
                  VSMessage::T_STATE,
                  0,
                  source_view,
                  next_seq,
                  new View(view))
    {
    }
};

struct VSDataMessage : VSMessage
{
    VSDataMessage(const ViewId& source_view,
                  const uint32_t seq,
                  const uint8_t user_type) :
        VSMessage(0,
                  VSMessage::T_DATA,
                  user_type,
                  source_view,
                  seq,
                  0)
    {

    }
};

struct VSLeaveMessage : VSMessage
{
    VSLeaveMessage(const ViewId& source_view,
                   const uint32_t seq) :
        VSMessage(0,
                  VSMessage::T_LEAVE,
                  0xff,
                  source_view,
                  seq,
                  0)
    {
    }
};

END_GCOMM_NAMESPACE

#endif // EVS_VS_MESSAGE_HPP
