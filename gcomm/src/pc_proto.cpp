
#include "pc_proto.hpp"

#include "pc_message.hpp"

BEGIN_GCOMM_NAMESPACE

#if 0
void PCProto::handle_first_trans(const View& view)
{
    if (start_prim == true)
    {
        if (view.get_members().length() > 1 || view.is_empty())
        {
            throw FatalException("");
        }
        if (get_uuid(view.get_members().begin()) != uuid)
        {
            throw FatalException("");
        }
    }
    else
    {
        // Just drop it
    }
}


void PCProto::handle_first_reg(const View& view)
{
    assert(get_state() == S_JOINING);

    if (start_prim == true)
    {

    }

}

#endif

void PCProto::handle_view(const View& view)
{
#if 0
    if (view.get_type() != View::V_TRANS && view.get_type() != View::V_REG)
    {
        throw FatalException("");
    }
    
    
    if (view.get_type() == View::V_TRANS)
    {
        if (get_state() == S_JOINING)
        {
            handle_first_trans(view);
        }
    }
    else
    {
        if (get_state() != S_
            handle_reg(view);
    }

#endif
}



void PCProto::handle_up(const int cid, const ReadBuf* rb, const size_t roff,
                        const ProtoUpMeta* um)
{
    const View* v = um->get_view();
    if (v)
    {
        handle_view(*v);
    }
}

int PCProto::handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
{
    return ENOTCONN;
}


END_GCOMM_NAMESPACE
