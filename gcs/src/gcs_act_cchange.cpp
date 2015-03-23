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
    memb(),
    uuid(GU_UUID_NIL),
    seqno(GCS_SEQNO_ILL),
    conf_id(-1),
    repl_proto_ver(-1),
    appl_proto_ver(-1)
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
    memb(),
    uuid(),
    seqno(),
    conf_id(),
    repl_proto_ver(),
    appl_proto_ver()
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
    int memb_num;
    is >> msg_ver >> c
       >> repl_proto_ver >> c
       >> appl_proto_ver >> c
       >> uuid >> c >> seqno >> c
       >> conf_id >> c
       >> memb_num;

    assert(cc_ver == msg_ver);

    b += str_len + 1; // memb array offset

    memb.reserve(memb_num);

    for (int i(0); i < memb_num; ++i)
    {
        gcs_act_cchange::member m;

        size_t id_len(::strlen(b));
        gu_uuid_scan(b, id_len, &m.uuid_);
        b += id_len + 1;

        m.name_ = b;
        b += m.name_.length() + 1;

        m.incoming_ = b;
        b += m.incoming_.length() + 1;

        const gcs_seqno_t* cached(reinterpret_cast<const gcs_seqno_t*>(b));
        m.cached_ = gu::gtoh<uint64_t>(*cached);
        b += sizeof(gcs_seqno_t);

        if (b[0] >= GCS_NODE_STATE_NON_PRIM && b[0] < GCS_NODE_STATE_MAX)
        {
            m.state_ = gcs_node_state(b[0]);
            ++b;
        }
        else
        {
            assert(0);
            gu_throw_error(EINVAL) << "Unrecognized node state in CC: " << b[0];
        }

        memb.push_back(m);
    }

    assert(b - static_cast<const char*>(cc_buf) == check_len);
}

static size_t
_memb_size(const std::vector<gcs_act_cchange::member>& m)
{
    size_t ret(0);

    for (size_t i(0); i < m.size(); ++i)
    {
        ret += GU_UUID_STR_LEN + 1;
        ret += m[i].name_.length() + 1;
        ret += m[i].incoming_.length() + 1;
        ret += sizeof(gcs_seqno_t); // lowest cached
        ret += sizeof(char);        // state
    }

    return ret;
}

static size_t
_strcopy(const std::string& str, char* ptr)
{
    std::copy(str.begin(), str.end(), ptr);
    return str.length();
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
       << memb.size();

    std::string const str(os.str());

    int const payload_len(str.length() + 1 + _memb_size(memb));
    int const check_offset(1 + payload_len);    // version byte + payload
    int const check_len(_checksum_len(cc_ver)); // checksum hash
    int const ret(check_offset + check_len);    // total message length

    /* using malloc() for C compatibility */
    *buf = ::malloc(ret);
    if (NULL == *buf)
    {
        gu_throw_error(ENOMEM) << "Failed to allocate " << ret
                               << " bytes for configuration change event.";
    }

    char* b(static_cast<char*>(*buf));

    assert(cc_ver < std::numeric_limits<unsigned char>::max());

    b[0] = cc_ver; ++b;

    b += _strcopy(str, b);
    b[0] = '\0'; ++b;

    for (size_t i(0); i < memb.size(); ++i)
    {
        const gcs_act_cchange::member& m(memb[i]);

        b += gu_uuid_print(&m.uuid_, b, GU_UUID_STR_LEN+1);
        b[0] = '\0'; ++b;
        b += _strcopy(m.name_, b);
        b[0] = '\0'; ++b;
        b += _strcopy(m.incoming_, b);
        b[0] = '\0'; ++b;

        gcs_seqno_t* const cached(reinterpret_cast<gcs_seqno_t*>(b));
        *cached = gu::htog<uint64_t>(m.cached_);
        b += sizeof(gcs_seqno_t);

        b[0] = m.state_; ++b;
    }

    assert(static_cast<char*>(*buf) + check_offset == b);

    gu::byte_t check[16];
    gu::FastHash::digest(*buf, check_offset, check);

    std::copy(check, check + check_len, b); b += check_len;

    assert(static_cast<char*>(*buf) + ret == b);

    return ret;
}

bool
gcs_act_cchange::member::operator==(const gcs_act_cchange::member& other) const
{
    return (
        uuid_     == other.uuid_     &&
        name_     == other.name_     &&
        incoming_ == other.incoming_ &&
        cached_   == other.cached_   &&
        state_    == other.state_
        );
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
        memb      == other.memb
        );
}

