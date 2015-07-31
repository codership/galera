/*
 * Copyright (C) 2015 Codership Oy <info@codership.com>
 */

#ifndef _gcs_code_msg_hpp_
#define _gcs_code_msg_hpp_

#include <gu_gtid.hpp>
#include <gu_macros.hpp>
#include <gu_byteswap.hpp>

#include <ostream>

namespace gcs
{
namespace core
{

/* helper class to hold code message in serialized form */
class CodeMsg
{
    union Msg
    {
        gu_byte_t buf_[32];
        struct
        {
            gu_uuid_t uuid_;
            int64_t   seqno_;
            int64_t   code_;
        } s_;
    } msg_;

    // ensure that union is properly packed
    GU_COMPILE_ASSERT(sizeof(Msg) == sizeof(Msg().buf_), msg_not_packed);

public:
    CodeMsg(const gu::GTID& gtid, int64_t code)
    {
        msg_.s_.uuid_  = gtid.uuid()();
        msg_.s_.seqno_ = gu::htog(gtid.seqno());
        msg_.s_.code_  = gu::htog(code);
    }

    void unserialize(gu::GTID& gtid, int64_t& code) const
    {
        gtid.set(msg_.s_.uuid_);
        gtid.set(gu::gtoh(msg_.s_.seqno_));
        code = gu::gtoh(msg_.s_.code_);
    }

    const gu_uuid_t& uuid() const { return msg_.s_.uuid_; }
    int64_t seqno()         const { return gu::gtoh(msg_.s_.seqno_); }
    int64_t code()          const { return gu::gtoh(msg_.s_.code_);  }

    const void* operator()() const { return &msg_; }

    static int serial_size() { return sizeof(Msg); }

    void print(std::ostream& os) const;

}; /* class CodeMsg */

static inline std::ostream&
operator << (std::ostream& os, const CodeMsg& msg)
{
    msg.print(os); return os;
}

} /* namespace core */

} /* namespace gcs */

#endif /* _gcs_code_msg_h_ */
