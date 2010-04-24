
#include "trx_handle.hpp"


void galera::TrxHandle::assign_local_seqno(wsrep_seqno_t seqno_l)
{
    seqno_l_ = seqno_l;
    wsdb_assign_trx_seqno(id_, seqno_l_, seqno_g_, state_, write_set_);
}

wsrep_seqno_t galera::TrxHandle::get_local_seqno() const
{
    return seqno_l_;
}

void galera::TrxHandle::assign_global_seqno(wsrep_seqno_t seqno_g)
{
    seqno_g_ = seqno_g;
    wsdb_assign_trx_seqno(id_, seqno_l_, seqno_g_, state_, write_set_);
}

wsrep_seqno_t galera::TrxHandle::get_global_seqno() const
{
    return seqno_g_;
}

void galera::TrxHandle::assign_state(enum wsdb_trx_state state)
{
    state_ = state;
    wsdb_assign_trx_state(id_, state_);
}

enum wsdb_trx_state galera::TrxHandle::get_state() const
{
    return state_;
}

void galera::TrxHandle::assign_position(enum wsdb_trx_position position)
{
    position_ = position;
    wsdb_assign_trx_pos(id_, position_);
}

enum wsdb_trx_position galera::TrxHandle::get_position() const
{
    return position_;
}

void galera::TrxHandle::assign_applier(void* applier, void* applier_ctx)
{
    applier_     = applier;
    applier_ctx_ = applier_ctx;
    wsdb_assign_trx_applier(id_, reinterpret_cast<job_worker*>(applier_), applier_ctx_);
}


int galera::TrxHandle::append_row_key(const void* dbtable,
                                      size_t dbtable_len,
                                      const void* key,
                                      size_t key_len, 
                                      int action)
{   struct wsdb_key_rec   wsdb_key;
    struct wsdb_table_key table_key;
    struct wsdb_key_part  key_part;
    
    wsdb_key.key             = &table_key;
    table_key.key_part_count = 1;
    table_key.key_parts      = &key_part;
    key_part.type            = WSDB_TYPE_VOID;
    
    /* assign key info */
    wsdb_key.dbtable     = (char*)dbtable;
    wsdb_key.dbtable_len = dbtable_len;
    key_part.length      = key_len;
    key_part.data        = (uint8_t*)key;

    return wsdb_append_row_key(id_, &wsdb_key, action);
}

void galera::TrxHandle::assign_write_set(struct wsdb_write_set* write_set)
{
    write_set_ = write_set;
}

struct wsdb_write_set* galera::TrxHandle::get_write_set(const void* row_buf, 
                                                        size_t row_buf_len)
{
    if (write_set_ == 0)
        write_set_ = wsdb_get_write_set(id_, 0, 
                                        (const char*)row_buf, row_buf_len);
    return write_set_;
}
