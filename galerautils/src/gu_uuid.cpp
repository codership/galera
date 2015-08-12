/*
 * Copyright (C) 2014-2015 Codership Oy <info@codership.com>
 *
 */

#include "gu_uuid.hpp"
#include "gu_throw.hpp"

size_t gu_uuid_from_string(const std::string& s, gu_uuid_t& uuid)
{
    ssize_t ret(gu_uuid_scan(s.c_str(), s.size(), &uuid));
    if (ret == -1) {
        gu_throw_error(EINVAL) << "could not parse UUID from '" << s << '\'';
    }
    return ret;
}

size_t gu_uuid_serialize(const gu_uuid_t& uuid, void* const buf,
                         size_t const buflen, size_t const offset)
{
    if (offset + gu_uuid_serial_size(uuid) > buflen)
        gu_throw_error (EMSGSIZE) << gu_uuid_serial_size(uuid)
                                  << " > " << (buflen - offset);

    return gu_uuid_serialize_unchecked(uuid, buf, buflen, offset);
}

size_t gu_uuid_unserialize(const void* const buf, size_t const buflen,
                           size_t const offset, gu_uuid_t& uuid)
{
    if (offset + gu_uuid_serial_size(uuid) > buflen)
        gu_throw_error (EMSGSIZE) << gu_uuid_serial_size(uuid)
                                  << " > " << (buflen - offset);

    return gu_uuid_unserialize_unchecked(buf, buflen, offset, uuid);
}

