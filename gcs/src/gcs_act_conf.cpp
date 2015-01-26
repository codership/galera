//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
//

#include "gcs.hpp"
#include "gu_digest.hpp"
#include "gu_hexdump.hpp"
#include "gu_uuid.hpp"

#include <sstream>
#include <string>
#include <limits>

gcs_act_cchange::gcs_act_cchange()
    :
    uuid(GU_UUID_NIL),
    seqno(GCS_SEQNO_ILL),
    conf_id(-1),
    memb(NULL),
    memb_size(0),
    memb_num(0),
    my_idx(-1),
    repl_proto_ver(-1),
    appl_proto_ver(-1),
    my_state(GCS_NODE_STATE_NON_PRIM)
{}

static int
_checksum_len(int ver)
{
    switch(ver)
    {
    case 0: return sizeof(uint64_t);
    default:
        gu_throw_error(EPROTO) << "Unsupported CC action version";
        throw;
    }
}

static inline gcs_node_state_t
_int_to_node_state(int const s)
{
    if (s < 0 || s >= GCS_NODE_STATE_MAX)
    {
        gu_throw_error(EINVAL) << "No such node state: " << s;
    }

    return gcs_node_state_t(s);
}

/* >> wrapper for gcs_node_state enum */
inline std::istream& operator>>(std::istream& is, gcs_node_state_t& ns)
{
    int s;
    is >> s;
    ns = _int_to_node_state(s);
    return is;
}

gcs_act_cchange::gcs_act_cchange(const void* const cc_buf, int const cc_size)
    :
    uuid(),
    seqno(),
    conf_id(),
    memb(NULL),
    memb_size(0),
    memb_num(),
    my_idx(),
    repl_proto_ver(),
    appl_proto_ver(),
    my_state()
{
    const char* b(static_cast<const char*>(cc_buf));
    int const cc_ver(b[0]);
    int const check_len(cc_size - _checksum_len(cc_ver));

    char check[16];
    gu::FastHash::digest(cc_buf, check_len, check);

    if (!std::equal(b + check_len, b + cc_size, check))
    {
        gu_throw_error(EINVAL) << "CC action checksum mismatch. Expected "
                               << gu::Hexdump(b + check_len, cc_size - check_len)
                               << ", found "
                               << gu::Hexdump(check, cc_size - check_len);
    }

    b += 1; // skip version byte

    int const str_len(::strlen(b));
    std::string const ist(b, str_len);
    std::istringstream is(ist);

    char c;
    int msg_ver;
    is >> msg_ver >> c
       >> repl_proto_ver >> c
       >> appl_proto_ver >> c
       >> uuid >> c >> seqno >> c
       >> conf_id >> c
       >> memb_num >> c
       >> my_idx >> c
       >> my_state;

    assert(cc_ver == msg_ver);

    b += str_len + 1; // memb array offset

    memb_size = check_len - 1 - str_len - 1;
    memb = static_cast<char*>(::operator new(memb_size));
    std::copy(b, b + memb_size, memb);
}

gcs_act_cchange::~gcs_act_cchange()
{
    delete[] memb;
}

int
gcs_act_cchange::write(void** buf) const
{
    int const cc_ver(0);
    std::ostringstream os;

    os << cc_ver << ','
       << repl_proto_ver << ','
       << appl_proto_ver << ','
       << uuid << ':' << seqno << ','
       << conf_id << ','
       << memb_num << ','
       << my_idx << ','
       << my_state;

    std::string const str(os.str());

    int const payload_len(str.length() + 1 + memb_size);
    int const check_off(1 + payload_len);     // version byte + payload
    int const sum_len(_checksum_len(cc_ver)); // checksum hash
    int const ret(check_off + sum_len);       // total message length

    /* using malloc() for C compatibility */
    *buf = ::malloc(ret);
    if (NULL == *buf)
    {
        gu_throw_error(ENOMEM) << "Failed to allocate " << memb_size
                               << " bytes for configuration change event.";
    }

    gu::byte_t* b(static_cast<gu::byte_t*>(*buf));

    assert(cc_ver < std::numeric_limits<gu::byte_t>::max());
    b[0] = cc_ver;
    std::copy(str.begin(), str.end(), b + 1);
    b[1 + str.length()] = '\0';
    std::copy(memb, memb + memb_size, b + str.length() + 2);

    gu::byte_t check[16];
    gu::FastHash::digest(b, check_off, check);

    std::copy(check, check + sum_len, b + check_off);

    return ret;
}

bool
gcs_act_cchange::operator==(const gcs_act_cchange& other) const
{
    return (
        repl_proto_ver == other.repl_proto_ver &&
        appl_proto_ver == other.appl_proto_ver &&
        uuid      == other.uuid      &&
        seqno     == other.seqno     &&
        conf_id   == other.conf_id   &&
        memb_num  == other.memb_num  &&
        my_idx    == other.my_idx    &&
        my_state  == other.my_state  &&
        memb_size == other.memb_size &&
        std::equal(memb, memb + memb_size, other.memb)
        );
}
