#ifndef PC_MESSAGE_HPP
#define PC_MESSAGE_HPP

#include "gcomm/view.hpp"
#include "gcomm/types.hpp"
#include "gcomm/common.hpp"
#include "gcomm/readbuf.hpp"
#include "gcomm/uuid.hpp"
#include "gcomm/map.hpp"

namespace gcomm
{
    class PCInst;
    class PCInstMap;
    class PCMessage;
    class PCUserMessage;
    class PCStateMessage;
    class PCInstallMessage;
    std::ostream& operator<<(std::ostream&, const PCInst&);
    std::ostream& operator<<(std::ostream&, const PCMessage&);
    bool operator==(const PCMessage&, const PCMessage&);
}

class gcomm::PCInst
{
    enum Flags { F_PRIM = 0x1 };

    bool    prim;
    int32_t last_seq;  // what is this seqno?
    ViewId  last_prim;
    int64_t to_seq;    // what is this seqno?

public:

    PCInst() : prim(false), 
               last_seq(-1), 
               last_prim(V_NON_PRIM), 
               to_seq(-1) 
    {}
    
    PCInst(const bool     prim_,
           const uint32_t last_seq_,
           const ViewId&  last_prim_, 
           const int64_t  to_seq_)
        :
        prim      (prim_),
        last_seq  (last_seq_),
        last_prim (last_prim_),
        to_seq    (to_seq_)
    {}
    
    void set_prim(const bool val)
    {
        prim = val;
    }
    
    bool get_prim() const
    {
        return prim;
    }
    
    void set_last_seq(const uint32_t seq)
    {
        last_seq = seq;
    }
    
    uint32_t get_last_seq() const
    {
        return last_seq;
    }
    
    void set_last_prim(const ViewId& last_prim_)
    {
        last_prim = last_prim_;
    }
    
    const ViewId& get_last_prim() const
    {
        return last_prim;
    }

    void set_to_seq(const uint64_t seq)
    {
        to_seq = seq;
    }

    int64_t get_to_seq() const
    {
        return to_seq;
    }

    size_t unserialize(const byte_t* buf, const size_t buflen, const size_t offset)
        throw (gu::Exception)
    {
        size_t   off = offset;
        uint32_t flags;

        gu_trace (off = gcomm::unserialize(buf, buflen, off, &flags));

        prim = flags & F_PRIM;

        gu_trace (off = gcomm::unserialize(buf, buflen, off, &last_seq));
        gu_trace (off = last_prim.unserialize(buf, buflen, off));
        gu_trace (off = gcomm::unserialize(buf, buflen, off, &to_seq));

        return off;
    }
    
    size_t serialize(byte_t* buf, const size_t buflen, const size_t offset) const
        throw (gu::Exception)
    {
        size_t   off   = offset;
        uint32_t flags = 0;

        flags |= prim ? F_PRIM : 0;

        gu_trace (off = gcomm::serialize(flags, buf, buflen, off));
        gu_trace (off = gcomm::serialize(last_seq, buf, buflen, off));
        gu_trace (off = last_prim.serialize(buf, buflen, off));
        gu_trace (off = gcomm::serialize(to_seq, buf, buflen, off));

        assert (serial_size() == (off - offset));

        return off;
    }

    static size_t serial_size()
    {
        PCInst* pcinst = reinterpret_cast<PCInst*>(0);

        //             flags
        return (sizeof(uint32_t) + sizeof(pcinst->last_seq) + 
                ViewId::serial_size() + sizeof(pcinst->to_seq));
    }

    bool operator==(const PCInst& cmp) const
    { 
        return get_prim()   == cmp.get_prim()      && 
            get_last_seq()  == cmp.get_last_seq()  &&
            get_last_prim() == cmp.get_last_prim() &&
            get_to_seq()    == cmp.get_to_seq();
    }


    
    std::string to_string() const
    {
        std::ostringstream ret;

        ret << "prim = "        << prim
            << ", last_seq = "  << last_seq 
            << ", last_prim = " << last_prim
            << ", to_seq = "    << to_seq;

        return ret.str();
    }    
};

inline std::ostream& gcomm::operator<<(std::ostream& os, const PCInst& inst)
{
    return (os << inst.to_string());
}

class gcomm::PCInstMap : public Map<UUID, PCInst> { };

class gcomm::PCMessage
{
public:

    enum Type {T_NONE, T_STATE, T_INSTALL, T_USER, T_MAX};

    static const char* to_string(Type t)
    {
        static const char* str[T_MAX] =
            { "NONE", "STATE", "INSTALL", "USER" };

        if (t < T_MAX) return str[t];

        return "unknown";
    }

private:

    int        version;
    Type       type;
    uint32_t   seq;
    PCInstMap* inst;

    PCMessage& operator=(const PCMessage&);

public:

    PCMessage() : version(-1), type(T_NONE), seq(0), inst(0) {}
    
    PCMessage(const int      version_, 
              const Type     type_,
              const uint32_t seq_)
        :
        version(version_),
        type   (type_),
        seq    (seq_),
        inst   (0)
    {
        if (type == T_STATE || type == T_INSTALL)
        {
            inst = new PCInstMap();
        }
    }
    
    PCMessage(const PCMessage& msg)
        :
        version (msg.version),
        type    (msg.type),
        seq     (msg.seq),
        inst    (msg.inst != 0 ? new PCInstMap(*msg.inst) : 0)
    {}
    
    virtual ~PCMessage() { delete inst; }
    
    size_t unserialize(const byte_t* buf, const size_t buflen, const size_t offset)
        throw (gu::Exception)
    {
        size_t   off;
        uint32_t b;

        delete inst;
        inst = 0;

        gu_trace (off = gcomm::unserialize(buf, buflen, offset, &b));

        version = b & 0xff;

        if (version != 0)
            gcomm_throw_runtime (EPROTONOSUPPORT)
                << "Unsupported protocol varsion: " << version;

        type = static_cast<Type>((b >> 8) & 0xff);

        if (type <= T_NONE || type >= T_MAX)
            gcomm_throw_runtime (EINVAL) << "Bad type value: " << type;

        gu_trace (off = gcomm::unserialize(buf, buflen, off, &seq));

        if (type == T_STATE || type == T_INSTALL)
        {
            inst = new PCInstMap();

            gu_trace (off = inst->unserialize(buf, buflen, off));
        }

        return off;
    }
    
    size_t serialize(byte_t* buf, const size_t buflen, const size_t offset) const
        throw (gu::Exception)
    {
        size_t   off;
        uint32_t b;

        b = type & 0xff;
        b <<= 8;
        b |= version & 0xff;

        gu_trace (off = gcomm::serialize(b, buf, buflen, offset));
        gu_trace (off = gcomm::serialize(seq, buf, buflen, off));


        if (type == T_STATE || type == T_INSTALL)
        {
            assert (inst);
            gu_trace (off = inst->serialize(buf, buflen, off));
        }
        else
        {
            assert (!inst);
        }

        assert (serial_size() == (off - offset));

        return off;        
    }
    
    size_t serial_size() const
    {
        //            header
        return sizeof(uint32_t) + sizeof(seq) 
            + (inst != 0 ? inst->serial_size() : 0);
    }
    
    int      get_version()  const { return version; }
    
    Type     get_type()     const { return type; }

    uint32_t get_seq()      const { return seq; }
    
    bool     has_inst_map() const { return inst; }

    // we have a problem here - we should not be able to construct the message
    // without the instance map in the first place. Or that should be another
    // class of message.
    const PCInstMap& get_inst_map() const
    {
        if (has_inst_map()) return *inst;

        gcomm_throw_fatal << "PC message does not have instance map"; throw;
    }

    PCInstMap& get_inst_map()
    {
        if (has_inst_map()) return *inst;

        gcomm_throw_fatal << "PC message does not have instance map"; throw;
    }

    std::string to_string() const
    {
        std::ostringstream ret;

        ret << "pcmsg( type: " << to_string(type) << ", seq: " << seq;

        if (has_inst_map())
        {
            ret << "," << get_inst_map();
        }

        ret << ')';

        return ret.str();
    }
};

inline std::ostream& gcomm::operator<<(std::ostream& os, const PCMessage& m)
{
    return (os << m.to_string());
}

class gcomm::PCStateMessage : public PCMessage
{
public:
    PCStateMessage() :  PCMessage(0, PCMessage::T_STATE, 0) {}
};

class gcomm::PCInstallMessage : public PCMessage
{
public:
    PCInstallMessage() : PCMessage(0, PCMessage::T_INSTALL, 0) {}
};

class gcomm::PCUserMessage : public PCMessage
{
    // @todo: why seq is not initialized from seq?
//    PCUserMessage(uint32_t seq) : PCMessage(0, PCMessage::T_USER, 0) {}
public:
    PCUserMessage(uint32_t seq) : PCMessage(0, PCMessage::T_USER, seq) {}
};


inline bool gcomm::operator==(const PCMessage& a, const PCMessage& b)
{
    bool ret =
        a.get_version() == b.get_version() &&
        a.get_type()    == b.get_type() &&
        a.get_seq()     == b.get_seq();
    
    if (ret == true)
    {
        if (a.has_inst_map() != b.has_inst_map())
        {
            // @todo: what is this supposed to mean? shouldn't it be just false?
            gcomm_throw_fatal;
        }
        
        if (a.has_inst_map())
        {
            ret = ret && a.get_inst_map() == b.get_inst_map();
        }
    }

    return ret;
}


#endif // PC_MESSAGE_HPP
