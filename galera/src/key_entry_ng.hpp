//
// Copyright (C) 2013 Codership Oy <info@codership.com>
//

#ifndef GALERA_KEY_ENTRY_NG_HPP
#define GALERA_KEY_ENTRY_NG_HPP

#include "trx_handle.hpp"

namespace galera
{
    class TrxHandle;

    class KeyEntryNG
    {
    public:
        KeyEntryNG(const KeySet::KeyPart& key)
            : refs_(), key_(key)
        {
            std::fill(&refs_[0],
                      &refs_[KeySet::Key::P_LAST],
                      reinterpret_cast<TrxHandle*>(NULL));
        }

        KeyEntryNG(const KeyEntryNG& other)
        : refs_(), key_(other.key_)
        {
            std::copy(&other.refs_[0],
                      &other.refs_[KeySet::Key::P_LAST],
                      &refs_[0]);
        }

        const KeySet::KeyPart& key() const { return key_; }

        void ref(KeySet::Key::Prefix p, const KeySet::KeyPart& k,
                 TrxHandle* trx)
        {
            assert(0 == refs_[p] ||
                   refs_[p]->global_seqno() <= trx->global_seqno());

            refs_[p] = trx;
            key_ = k;
        }

        void unref(KeySet::Key::Prefix p, TrxHandle* trx)
        {
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
            bool ret(refs_[0] != NULL);

            for (int i(1); false == ret && i <= KeySet::Key::P_LAST; ++i)
            {
                ret = (refs_[i] != NULL);
            }

            return ret;
        }

        const TrxHandle* ref_trx(KeySet::Key::Prefix p) const
        {
            return refs_[p];
        }

        size_t size() const
        {
            return sizeof(*this);
        }

        void swap(KeyEntryNG& other) throw()
        {
            using std::swap;
            swap(refs_, other.refs_);
            swap(key_,  other.key_);
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

        TrxHandle*      refs_[KeySet::Key::P_LAST + 1];
        KeySet::KeyPart key_;

#ifndef NDEBUG
        void assert_ref(KeySet::Key::Prefix, TrxHandle*) const;
        void assert_unref(KeySet::Key::Prefix, TrxHandle*) const;
#endif /* NDEBUG */
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

