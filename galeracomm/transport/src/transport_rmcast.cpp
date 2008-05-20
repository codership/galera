
#include "transport_rmcast.hpp"





void RMCASTTransport::connect(const char *addr)
{

}


void RMCASTTransport::close()
{

}


void RMCASTTransport::listen(const char *addr)
{

}

Transport *RMCASTTransport::accept(Poll *, Protolay *)
{
    Transport *ret = 0;
    return ret;
}


int RMCASTTransport::handle_down(WriteBuf *, const ProtoDownMeta *)
{
    int err = 0;
    return err;
}

int RMCASTTransport::handle_pending()
{

}

