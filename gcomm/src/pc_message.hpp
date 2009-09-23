#ifndef PC_MESSAGE_HPP
#define PC_MESSAGE_HPP

#include "gcomm/view.hpp"
#include "gcomm/types.hpp"
#include "gcomm/common.hpp"
#include "gcomm/readbuf.hpp"
#include "gcomm/uuid.hpp"

#include "inst_map.hpp"

BEGIN_GCOMM_NAMESPACE

class PCInst
{
    enum Flags { F_PRIM = 0x1 };

    bool    prim;
    int32_t last_seq;  // what is this seqno?
    ViewId  last_prim;
    int64_t to_seq;    // what is this seqno?

public:

    PCInst() : prim(false), last_seq(-1), last_prim(), to_seq(-1) {}
    
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

    size_t read(const byte_t* buf, const size_t buflen, const size_t offset)
    {
        size_t off = offset;
        uint32_t b;

        if ((off = gcomm::read(buf, buflen, off, &b)) == 0)        return 0;

        prim = b & F_PRIM;

        if ((off = gcomm::read(buf, buflen, off, &last_seq)) == 0) return 0;

        if ((off = last_prim.read(buf, buflen, off)) == 0)         return 0;

        if ((off = gcomm::read(buf, buflen, off, &to_seq)) == 0)   return 0;

        return off;
    }
    
    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const
    {
        size_t off = offset;
        uint32_t b = 0;

        b |= prim ? F_PRIM : 0;

        if ((off = gcomm::write(b, buf, buflen, off)) == 0)
            gcomm_throw_fatal;

        if ((off = gcomm::write(last_seq, buf, buflen, off)) == 0)
            gcomm_throw_fatal;

        gu_trace (off = last_prim.write(buf, buflen, off));

        if ((off = gcomm::write(to_seq, buf, buflen, off)) == 0)
            gcomm_throw_fatal;

        return off;
    }

    static size_t size()
    {
        PCInst* pcinst = reinterpret_cast<PCInst*>(0);

        return (sizeof(pcinst->prim) + sizeof(pcinst->last_seq) + 
                ViewId::size() + sizeof(pcinst->to_seq));
    }

    string to_string() const
    {
        return "prim = "       + make_int(prim).to_string()
            + ", last_seq = "  + make_int(last_seq).to_string() 
            + ", last_prim = " + last_prim.to_string()
            + ", to_seq = "    + make_int(to_seq).to_string();
    }    
};

inline bool operator==(const PCInst& a, const PCInst& b)
{
    return a.get_prim()   == b.get_prim()      && 
        a.get_last_seq()  == b.get_last_seq()  &&
        a.get_last_prim() == b.get_last_prim() &&
        a.get_to_seq()    == b.get_to_seq();
}

typedef InstMap<PCInst> PCInstMap;

class PCMessage
{
public:

    enum Type {T_NONE, T_STATE, T_INSTALL, T_USER, T_MAX};

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
    
    size_t read(const byte_t* buf, const size_t buflen, const size_t offset)
    {
        size_t   off;
        uint32_t b;

        delete inst;
        inst = 0;

        if ((off = gcomm::read(buf, buflen, offset, &b)) == 0)
            return 0;

        version = b & 0xff;

        if (version != 0) return 0;

        type = static_cast<Type>((b >> 8) & 0xff);

        if (type <= T_NONE || type >= T_MAX) return 0;

        if ((off = gcomm::read(buf, buflen, off, &seq)) == 0) return 0;

        if (type == T_STATE || type == T_INSTALL)
        {
            inst = new PCInstMap();

            if ((off = inst->read(buf, buflen, off)) == 0) return 0;
        }

        return off;
    }
    
    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const
    {
        size_t off;
        uint32_t b;

        b = type & 0xff;
        b <<= 8;
        b |= version & 0xff;

        if ((off = gcomm::write(b, buf, buflen, offset)) == 0)
            gcomm_throw_fatal << buflen;

        if ((off = gcomm::write(seq, buf, buflen, off)) == 0)
            gcomm_throw_fatal << buflen;

        gu_trace (off = inst->write(buf, buflen, off));

        return off;        
    }
    
    size_t size() const
    {
        //            header
        return sizeof(uint32_t) + sizeof(seq) + (inst != 0 ? inst->size() : 0);
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

    string to_string() const
    {
        std::ostringstream ret;

        ret << "pcmsg(" << get_type() << ", " << get_seq();

        if (has_inst_map())
        {
            ret << ", " << get_inst_map().to_string();
        }

        ret << ')';

        return ret.str();
    }
};


struct PCStateMessage   : PCMessage
{
    PCStateMessage() :  PCMessage(0, PCMessage::T_STATE, 0) {}
};

struct PCInstallMessage : PCMessage
{
    PCInstallMessage() : PCMessage(0, PCMessage::T_INSTALL, 0) {}
};

struct PCUserMessage    : PCMessage
{
    // @todo: why seq is not initialized from seq?
//    PCUserMessage(uint32_t seq) : PCMessage(0, PCMessage::T_USER, 0) {}
    PCUserMessage(uint32_t seq) : PCMessage(0, PCMessage::T_USER, seq) {}
};


inline bool operator==(const PCMessage& a, const PCMessage& b)
{
    bool ret = a.get_version() == b.get_version() &&
        a.get_type() == b.get_type() &&
        a.get_seq() == b.get_seq();

    if (ret == true)
    {
        if (a.has_inst_map() != b.has_inst_map())
        {
            // @todo: what is this supposed to mean?
            gcomm_throw_fatal;
        }

        if (a.has_inst_map())
        {
            ret = ret && a.get_inst_map() == b.get_inst_map();
        }
    }

    return ret;
}

END_GCOMM_NAMESPACE

#endif // PC_MESSAGE_HPP
