//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
//

#include "gcs.hpp"
#include "gu_digest.hpp"
#include "gu_hexdump.hpp"
#include "gu_uuid.hpp"
#include "gu_macros.hpp"

#include <sstream>
#include <string>
#include <limits>

gcs_act_cchange::gcs_act_cchange()
    :
    memb          (),
    uuid          (GU_UUID_NIL),
    seqno         (GCS_SEQNO_ILL),
    conf_id       (-1),
    repl_proto_ver(-1),
    appl_proto_ver(-1)
{}

enum Version
{
    VER0 = 0
};

static Version
_version(int ver)
{
    switch(ver)
    {
    case VER0: return VER0;
    default:
        gu_throw_error(EPROTO) << "Unsupported CC action version";
        throw;
    }
}

// sufficiently big array to cover all potential checksum sizes
typedef char checksum_t[16];

static inline int
_checksum_len(Version const ver)
{
    int ret(0);

    switch(ver)
    {
    case VER0: ret = 8; break;
    default: assert(0);
    }

    assert(ret < int(sizeof(checksum_t)));

    return ret;
}

static void
_checksum(Version const ver, const void* const buf, size_t const size,
          checksum_t& res)
{
    switch(ver)
    {
    case VER0: gu::FastHash::digest(buf, size, res); return;
    default: assert(0);
    }
}

static inline gcs_node_state_t
_int_to_node_state(int const s)
{
    if (gu_unlikely(s < 0 || s >= GCS_NODE_STATE_MAX))
    {
        assert(0);
        gu_throw_error(EINVAL) << "No such node state: " << s;
    }

    return gcs_node_state_t(s);
}

gcs_act_cchange::gcs_act_cchange(const void* const cc_buf, int const cc_size)
    :
    memb          (),
    uuid          (),
    seqno         (),
    conf_id       (),
    repl_proto_ver(),
    appl_proto_ver()
{
    const char* b(static_cast<const char*>(cc_buf));
    Version const cc_ver(_version(b[0]));
    int const check_len(_checksum_len(cc_ver));
    int const check_offset(cc_size - check_len);

    checksum_t check;
    _checksum(cc_ver, cc_buf, check_offset, check);

    if (gu_unlikely(!std::equal(b + check_offset, b + cc_size, check)))
    {
        gu_throw_error(EINVAL) << "CC action checksum mismatch. Expected "
                               << gu::Hexdump(b + check_offset, check_len)
                               << ", computed "
                               << gu::Hexdump(check, check_len);
    }

    b += 1; // skip version byte

    int const str_len(::strlen(b));
    std::string const ist(b, str_len);
    std::istringstream is(ist);

    char c;
    int msg_ver;
    int memb_num;
    uint64_t unused;
    is >> msg_ver >> c
       >> repl_proto_ver >> c
       >> appl_proto_ver >> c
       >> uuid >> c >> seqno >> c
       >> conf_id >> c
       >> unused >> c
       >> unused >> c
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

        m.state_ = _int_to_node_state(b[0]);
        ++b;

        memb.push_back(m);
    }

    assert(b - static_cast<const char*>(cc_buf) == check_offset);
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
    Version const cc_ver(VER0);
    std::ostringstream os;

    os << cc_ver << ','
       << repl_proto_ver << ','
       << appl_proto_ver << ','
       << uuid << ':' << seqno << ','
       << conf_id << ','
       << GCS_SEQNO_ILL << ','
       <<  0 << ','
       << memb.size();

    std::string const str(os.str());

    int const payload_len(str.length() + 1 + _memb_size(memb));
    int const check_offset(1 + payload_len);    // version byte + payload
    int const check_len(_checksum_len(cc_ver)); // checksum length
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

    checksum_t check;
    _checksum(cc_ver, *buf, check_offset, check);

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

