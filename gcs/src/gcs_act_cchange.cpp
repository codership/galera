//
// Copyright (C) 2015-2017 Codership Oy <info@codership.com>
//

#include "gcs.hpp"
#include "gu_digest.hpp"
#include "gu_hexdump.hpp"
#include "gu_uuid.hpp"
#include "gu_macros.hpp"
#include "gu_logger.hpp"

#include "gu_limits.h"

#include <sstream>
#include <string>
#include <limits>

gcs_act_cchange::gcs_act_cchange()
    :
    memb          (),
    uuid          (GU_UUID_NIL),
    seqno         (GCS_SEQNO_ILL),
    conf_id       (-1),
    vote_seqno    (GCS_SEQNO_ILL),
    vote_res      (0),
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
    vote_seqno    (),
    vote_res      (),
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
        std::vector<char> debug(check_offset);
        std::copy(b + 1, b + check_offset, debug.begin());
        debug[check_offset - 1] = '\0';

        gu_throw_error(EINVAL) << "CC action checksum mismatch. Found "
                               << gu::Hexdump(b + check_offset, check_len)
                               << " at offset " << check_offset
                               << ", computed "
                               << gu::Hexdump(check, sizeof(check))
                               << ", action contents: '" << debug.data() << "'";
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
       >> vote_seqno >> c
       >> vote_res >> c
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

        b += gu::unserialize8(b, 0, m.cached_);

        m.state_ = _int_to_node_state(b[0]);
        ++b;

        memb.push_back(m);
    }

    assert(b - static_cast<const char*>(cc_buf) <= check_offset);
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
       << vote_seqno << ','
       << vote_res << ','
       << memb.size();

    std::string const str(os.str());
    int const payload_len(1 + str.length() + 1 + _memb_size(memb));
    int const check_len(_checksum_len(cc_ver)); // checksum length

    // total message length, with necessary padding for alignment
    int const ret(GU_ALIGN(payload_len + check_len, GU_MIN_ALIGNMENT));

    /* using malloc() for C compatibility */
    *buf = ::malloc(ret);
    if (NULL == *buf)
    {
        gu_throw_error(ENOMEM) << "Failed to allocate " << ret
                               << " bytes for configuration change event.";
    }
    ::memset(*buf, 0, ret); // initialize

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

        b += gu::serialize8(m.cached_, b, 0);

        b[0] = m.state_; ++b;
    }

    int const check_offset(ret - check_len); // writing checksum to the end
    assert(gu::ptr_offset(*buf, check_offset) >= b);
    b = static_cast<char*>(gu::ptr_offset(*buf, check_offset));

    checksum_t check;
    _checksum(cc_ver, *buf, check_offset, check);

    log_debug << "Writing down CC checksum: "
              << gu::Hexdump(check, sizeof(check))
              << " at offset " << check_offset;

    std::copy(check, check + check_len, b); b += check_len;

    assert(gu::ptr_offset(*buf, ret) == b);

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

std::ostream&
operator <<(std::ostream& os, const struct gcs_act_cchange& cc)
{
    os << "Version(repl,appl): "
        << cc.repl_proto_ver << ',' << cc.appl_proto_ver << '\n'
        << "GTID: " << cc.uuid << ':' << cc.seqno << ", "
        << "conf ID: " << cc.conf_id << '\n'
        << "Vote(seqno:res): " << cc.vote_seqno << ':' << cc.vote_res << '\n'
        << "Members #: " << cc.memb.size();
    return os;
}
