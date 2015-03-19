//
// Copyright (C) 2012 Codership Oy <info@codership.com>
//

#ifndef GALERA_KEY_ENTRY_OS_HPP
#define GALERA_KEY_ENTRY_OS_HPP

#include "key_os.hpp"

namespace galera
{
    class TrxHandleSlave;

    class KeyEntryOS
    {
    public:
        KeyEntryOS(const KeyOS& row_key)
            :
            key_(row_key),
            ref_trx_(0),
            ref_full_trx_(0),
            ref_shared_trx_(0),
            ref_full_shared_trx_(0)
        {}

        template <class Ci>
        KeyEntryOS(int version, Ci begin, Ci end, uint8_t flags)
            :
            key_(version, begin, end, flags),
            ref_trx_(0),
            ref_full_trx_(0),
            ref_shared_trx_(0),
            ref_full_shared_trx_(0)
        {}

        KeyEntryOS(const KeyEntryOS& other)
            :
            key_(other.key_),
            ref_trx_(other.ref_trx_),
            ref_full_trx_(other.ref_full_trx_),
            ref_shared_trx_(other.ref_shared_trx_),
            ref_full_shared_trx_(other.ref_full_shared_trx_)
        {}

        ~KeyEntryOS()
        {
            assert(ref_trx_ == 0);
            assert(ref_full_trx_ == 0);
            assert(ref_shared_trx_ == 0);
            assert(ref_full_shared_trx_ == 0);
        }

        const KeyOS& get_key() const { return key_; }
        const KeyOS& get_key(int version) const { return key_; }

        void ref(TrxHandleSlave* trx, bool full_key)
        {
#ifndef NDEBUG
            assert_ref(trx, full_key);
#endif /* NDEBUG */
            ref_trx_ = trx;
            if (full_key == true)
            {
                ref_full_trx_ = trx;
            }
        }

        void unref(TrxHandleSlave* trx, bool full_key)
        {
            assert(ref_trx_ != 0);
            if (ref_trx_ == trx) ref_trx_ = 0;
            if (full_key == true && ref_full_trx_ == trx)
            {
                ref_full_trx_ = 0;
            }
            else
            {
#ifndef NDEBUG
                assert_unref(trx);
#endif /* NDEBUG */
            }
        }

        void ref_shared(TrxHandleSlave* trx, bool full_key)
        {
#ifndef NDEBUG
            assert_ref_shared(trx, full_key);
#endif /* NDEBUG */
            ref_shared_trx_ = trx;
            if (full_key == true)
            {
                ref_full_shared_trx_ = trx;
            }
        }

        void unref_shared(TrxHandleSlave* trx, bool full_key)
        {
            assert(ref_shared_trx_ != 0);
            if (ref_shared_trx_ == trx) ref_shared_trx_ = 0;
            if (full_key == true && ref_full_shared_trx_ == trx)
            {
                ref_full_shared_trx_ = 0;
            }
            else
            {
#ifndef NDEBUG
                assert_unref_shared(trx);
#endif /* NDEBUG */
            }
        }

        const TrxHandleSlave* ref_trx() const { return ref_trx_; }
        const TrxHandleSlave* ref_full_trx() const { return ref_full_trx_; }
        const TrxHandleSlave* ref_shared_trx() const { return ref_shared_trx_; }
        const TrxHandleSlave* ref_full_shared_trx() const { return ref_full_shared_trx_; }

        size_t size() const
        {
            return key_.size() + sizeof(*this);
        }

    private:
        void operator=(const KeyEntryOS&);
        KeyOS      key_;
        TrxHandleSlave* ref_trx_;
        TrxHandleSlave* ref_full_trx_;
        TrxHandleSlave* ref_shared_trx_;
        TrxHandleSlave* ref_full_shared_trx_;

#ifndef NDEBUG
        void assert_ref(TrxHandleSlave*, bool) const;
        void assert_unref(TrxHandleSlave*) const;
        void assert_ref_shared(TrxHandleSlave*, bool) const;
        void assert_unref_shared(TrxHandleSlave*) const;
#endif /* NDEBUG */
    };

    class KeyEntryPtrHash
    {
    public:
        size_t operator()(const KeyEntryOS* const ke) const
        {
            return ke->get_key().hash();
        }
    };

    class KeyEntryPtrHashAll
    {
    public:
        size_t operator()(const KeyEntryOS* const ke) const
        {
            return ke->get_key().hash_with_flags();
        }
    };

    class KeyEntryPtrEqual
    {
    public:
        bool operator()(const KeyEntryOS* const left, const KeyEntryOS* const right)
            const
        {
            return left->get_key() == right->get_key();
        }
    };

    class KeyEntryPtrEqualAll
    {
    public:
        bool operator()(const KeyEntryOS* const left, const KeyEntryOS* const right)
            const
        {
            return left->get_key().equal_all(right->get_key());
        }
    };

}

#endif // GALERA_KEY_ENTRY_HPP

