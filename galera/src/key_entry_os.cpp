//
// Copyright (C) 2012 Codership Oy <info@codership.com>
//

#include "key_entry_os.hpp"
#include "trx_handle.hpp"

namespace galera
{

#ifndef NDEBUG

void
KeyEntryOS::assert_ref(TrxHandle* trx, bool full_key) const
{
    assert(ref_trx_ == 0 ||
           ref_trx_->global_seqno() <= trx->global_seqno());
    if (full_key)
    {
        assert(ref_full_trx_ == 0 ||
               (ref_full_trx_->global_seqno() <= trx->global_seqno() &&
                ref_trx_ != 0));
    }
}

void
KeyEntryOS::assert_unref(TrxHandle* trx) const
{
    if (ref_full_trx_ != 0 && ref_trx_ == 0)
    {
        log_fatal << "dereferencing EXCLUSIVE partial key: " << key_
                  << " by " << trx->global_seqno()
                  << ", while full key referenced by "
                  << ref_full_trx_->global_seqno();
        assert(0);
    }
}

void
KeyEntryOS::assert_ref_shared(TrxHandle* trx, bool full_key) const
{
    assert(ref_shared_trx_ == 0 ||
           ref_shared_trx_->global_seqno() <= trx->global_seqno());
    if (full_key)
    {
        assert(ref_full_shared_trx_ == 0 ||
               (ref_full_shared_trx_->global_seqno() <= trx->global_seqno() &&
                ref_shared_trx_ != 0));
    }
}

void
KeyEntryOS::assert_unref_shared(TrxHandle* trx) const
{
    if (ref_full_shared_trx_ != 0 && ref_shared_trx_ == 0)
    {
        log_fatal << "dereferencing SHARED partial key: " << key_
                  << " by " << trx->global_seqno()
                  << ", while full key referenced by "
                  << ref_full_shared_trx_->global_seqno();
        assert(0);
    }
}

#endif /* NDEBUG */

}


