//
// Copyright (C) 2013-2018 Codership Oy <info@codership.com>
//

#ifndef GALERA_KEY_ENTRY_NG_HPP
#define GALERA_KEY_ENTRY_NG_HPP

#include "trx_handle.hpp"

namespace galera
{
    class KeyEntryNG
    {
    public:
        KeyEntryNG(const KeySet::KeyPart& key)
            : refs_{nullptr, nullptr, nullptr, nullptr},
#ifndef NDEBUG
              seqnos_{0, 0, 0, 0},
#endif // NDEBUG
              key_(key)
        {
        }

        KeyEntryNG(const KeyEntryNG& other)
            : refs_(other.refs_)
            ,
#ifndef NDEBUG
            seqnos_(other.seqnos_)
            ,
#endif /* NDEBUG */
            key_(other.key_)
        {
        }

        const KeySet::KeyPart& key() const { return key_; }

        void ref(wsrep_key_type_t p, const KeySet::KeyPart& k,
                 TrxHandleSlave* trx)
        {
            assert(p <=  KeySet::Key::TYPE_MAX);
            assert(0 == refs_[p] ||
                   refs_[p]->global_seqno() <= trx->global_seqno());

            refs_[p] = trx;
#ifndef NDEBUG
            seqnos_[p] = trx->global_seqno();
#endif // NDEBUG
            key_ = k;
        }

        void unref(wsrep_key_type_t p, const TrxHandleSlave* trx)
        {
            assert(p <=  KeySet::Key::TYPE_MAX);
            assert(refs_[p] != NULL);

            if (refs_[p] == trx)
            {
                refs_[p] = NULL;
            }
            else
            {
                assert(refs_[p]->global_seqno() > trx->global_seqno());
                assert(0);
            }
        }

        bool referenced() const
        {
            for (auto i : refs_)
            {
                if (i != nullptr) return true;
            }
            return false;
        }

        void
        for_each_ref(const std::function<void(const TrxHandleSlave*)>& fn) const
        {
            for (auto i : refs_)
            {
                fn(i);
            }
        }

        const TrxHandleSlave* ref_trx(wsrep_key_type_t const p) const
        {
            assert(p <=  KeySet::Key::TYPE_MAX);
            return refs_[p];
        }

        size_t size() const
        {
            return sizeof(*this);
        }

        void swap(KeyEntryNG& other) throw()
        {
            std::swap(refs_, other.refs_);
#ifndef NDEBUG
            std::swap(seqnos_, other.seqnos_);
#endif /* NDEBUG */
            std::swap(key_,  other.key_);
        }

        KeyEntryNG& operator=(KeyEntryNG ke)
        {
            swap(ke);
            return *this;
        }

        ~KeyEntryNG()
        {
            assert(!referenced());
        }

    private:
        std::array<TrxHandleSlave*, KeySet::Key::TYPE_MAX + 1> refs_;
#ifndef NDEBUG
        std::array<wsrep_seqno_t, KeySet::Key::TYPE_MAX + 1> seqnos_;
#endif // NDEBUG
        KeySet::KeyPart key_;

    };

    inline void swap(KeyEntryNG& a, KeyEntryNG& b) { a.swap(b); }

    class KeyEntryHashNG
    {
    public:
        size_t operator()(const KeyEntryNG& ke) const
        {
            return ke.key().hash();
        }
    };

    class KeyEntryPtrHashNG
    {
    public:
        size_t operator()(const KeyEntryNG* const ke) const
        {
            return ke->key().hash();
        }
    };

    class KeyEntryEqualNG
    {
    public:
        bool operator()(const KeyEntryNG& left,
                        const KeyEntryNG& right)
            const
        {
            return left.key().matches(right.key());
        }
    };

    class KeyEntryPtrEqualNG
    {
    public:
        bool operator()(const KeyEntryNG* const left,
                        const KeyEntryNG* const right)
            const
        {
            return left->key().matches(right->key());
        }
    };
}

#endif // GALERA_KEY_ENTRY_HPP

