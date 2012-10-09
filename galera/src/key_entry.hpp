//
// Copyright (C) 2012 Codership Oy <info@codership.com>
//

#ifndef GALERA_KEY_ENTRY_HPP
#define GALERA_KEY_ENTRY_HPP

#include "key.hpp"

namespace galera
{
    class TrxHandle;

    class KeyEntry
    {
    public:
        KeyEntry(const Key& row_key)
            :
            key_(row_key),
            ref_trx_(0),
            ref_full_trx_(0),
            ref_shared_trx_(0),
            ref_full_shared_trx_(0)
        {}

        template <class Ci>
        KeyEntry(int version, Ci begin, Ci end, uint8_t flags)
            :
            key_(version, begin, end, flags),
            ref_trx_(0),
            ref_full_trx_(0),
            ref_shared_trx_(0),
            ref_full_shared_trx_(0)
        {}

        KeyEntry(const KeyEntry& other)
            :
            key_(other.key_),
            ref_trx_(other.ref_trx_),
            ref_full_trx_(other.ref_full_trx_),
            ref_shared_trx_(other.ref_shared_trx_),
            ref_full_shared_trx_(other.ref_full_shared_trx_)
        {}

        ~KeyEntry()
        {
            assert(ref_trx_ == 0);
            assert(ref_full_trx_ == 0);
            assert(ref_shared_trx_ == 0);
            assert(ref_full_shared_trx_ == 0);
        }

        const Key& get_key() const { return key_; }
        const Key& get_key(int version) const { return key_; }

        void ref(TrxHandle* trx, bool full_key)
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

        void unref(TrxHandle* trx, bool full_key)
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

        void ref_shared(TrxHandle* trx, bool full_key)
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

        void unref_shared(TrxHandle* trx, bool full_key)
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

        const TrxHandle* ref_trx() const { return ref_trx_; }
        const TrxHandle* ref_full_trx() const { return ref_full_trx_; }
        const TrxHandle* ref_shared_trx() const { return ref_shared_trx_; }
        const TrxHandle* ref_full_shared_trx() const { return ref_full_shared_trx_; }

        size_t size() const
        {
            return key_.size() + sizeof(*this);
        }

    private:
        void operator=(const KeyEntry&);
        Key        key_;
        TrxHandle* ref_trx_;
        TrxHandle* ref_full_trx_;
        TrxHandle* ref_shared_trx_;
        TrxHandle* ref_full_shared_trx_;

#ifndef NDEBUG
        void assert_ref(TrxHandle*, bool);
        void assert_unref(TrxHandle*);
        void assert_ref_shared(TrxHandle*, bool);
        void assert_unref_shared(TrxHandle*);
#endif /* NDEBUG */
    };

    class KeyEntryPtrHash
    {
    public:
        size_t operator()(const KeyEntry* const ke) const
        {
            return ke->get_key().hash();
        }
    };

    class KeyEntryPtrHashAll
    {
    public:
        size_t operator()(const KeyEntry* const ke) const
        {
            return ke->get_key().hash_with_flags();
        }
    };

    class KeyEntryPtrEqual
    {
    public:
        bool operator()(const KeyEntry* const left, const KeyEntry* const right)
            const
        {
            return left->get_key() == right->get_key();
        }
    };

    class KeyEntryPtrEqualAll
    {
    public:
        bool operator()(const KeyEntry* const left, const KeyEntry* const right)
            const
        {
            return left->get_key().equal_all(right->get_key());
        }
    };

}

#endif // GALERA_KEY_ENTRY_HPP

