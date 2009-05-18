
#include "gcomm/util.hpp"
#include "gcomm/exception.hpp"
#include "gcomm/logger.hpp"

#include <istream>

using std::istringstream;

BEGIN_GCOMM_NAMESPACE

bool read_bool(const string& s)
{
    istringstream is(s);
    bool ret;
    if ((is >> ret).fail())
    {
        LOG_FATAL("string '" + s + "' does not contain bool");
        throw FatalException("");
    }
    return ret;
}

int read_int(const string& s)
{
    istringstream is(s);
    int ret;
    if ((is >> ret).fail())
    {
        LOG_FATAL("string '" + s + "' does not contain int");
        throw FatalException("");
    }
    return ret;
}

long read_long(const string& s)
{
    istringstream is(s);
    long ret;
    if ((is >> ret).fail())
    {
        LOG_FATAL("string '" + s + "' does not contain long");
        throw FatalException("");
    }
    return ret;
}

END_GCOMM_NAMESPACE
