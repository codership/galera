//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

//!
// @file gu_unordered.hpp unordered_[multi]map definition
//
// We still have environments where neither boost or std unordered
// stuff is available. Wrapper classes are provided for alternate
// implementations with standard semantics.
//
// For usage see either boost or tr1 specifications for unordered_[multi]map
//

#ifndef GU_UNORDERED_HPP
#define GU_UNORDERED_HPP

#if defined(HAVE_BOOST_UNORDERED_MAP_HPP)
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#elif defined(HAVE_UNORDERED_MAP)
#include <unordered_map>
#include <unordered_set>
#elif defined(HAVE_TR1_UNORDERED_MAP)
#include <tr1/unordered_map>
#include <tr1/unordered_set>
#else
#error "no unordered map available"
#endif

#include "gu_throw.hpp"

namespace gu
{
    template <typename K>
    class UnorderedHash
    {
    public:
#if defined(HAVE_BOOST_UNORDERED_MAP_HPP)
        typedef boost::hash<K> Type;
#elif defined(HAVE_UNORDERED_MAP)
        typedef std::hash<K> Type;
#elif defined(HAVE_TR1_UNORDERED_MAP)
        typedef std::tr1::hash<K> Type;
#endif
        size_t operator()(const K& k) const
        {
            return Type()(k);
        }
    };


    template <typename K>
    size_t HashValue(const K& key)
    {
        return UnorderedHash<K>()(key);
    }

    template <typename K, typename H = UnorderedHash<K>,
              class P = std::equal_to<K>,
              class A = std::allocator<K> >
    class UnorderedSet
    {
#if defined(HAVE_BOOST_UNORDERED_MAP_HPP)
        typedef boost::unordered_set<K, H, P, A> type;
#elif defined(HAVE_UNORDERED_MAP)
        typedef std::unordered_set<K, H, P, A> type;
#elif defined(HAVE_TR1_UNORDERED_MAP)
        typedef std::tr1::unordered_set<K, H, P, A> type;
#endif
        type impl_;
    public:
        typedef typename type::value_type value_type;
        typedef typename type::iterator iterator;
        typedef typename type::const_iterator const_iterator;

        UnorderedSet() : impl_() { }
        explicit UnorderedSet(A a) : impl_(a) { }

        iterator begin() { return impl_.begin(); }
        const_iterator begin() const { return impl_.begin(); }
        iterator end() { return impl_.end(); }
        const_iterator end() const { return impl_.end(); }
        std::pair<iterator, bool> insert(const value_type& k)
        { return impl_.insert(k); }
        iterator insert_unique(const value_type& k)
        {
            std::pair<iterator, bool> ret(insert(k));
            if (ret.second == false) gu_throw_fatal << "insert unique failed";
            return ret.first;
        }
        iterator find(const K& key) { return impl_.find(key); }
        const_iterator find(const K& key) const { return impl_.find(key); }
        iterator erase(iterator i) { return impl_.erase(i); }
        size_t size() const { return impl_.size(); }
        bool empty() const { return impl_.empty(); }
        void clear() { impl_.clear(); }
        void rehash(size_t n) { impl_.rehash(n); }
#if defined(HAVE_UNORDERED_MAP)
        void reserve(size_t n) { impl_.reserve(n); }
#endif
    };


    template <typename K, typename V, typename H = UnorderedHash<K>,
              class P = std::equal_to<K>,
              class A = std::allocator<std::pair<const K, V> > >
    class UnorderedMap
    {
#if defined(HAVE_BOOST_UNORDERED_MAP_HPP)
        typedef boost::unordered_map<K, V, H, P, A> type;
#elif defined(HAVE_UNORDERED_MAP)
        typedef std::unordered_map<K, V, H, P, A> type;
#elif defined(HAVE_TR1_UNORDERED_MAP)
        typedef std::tr1::unordered_map<K, V, H, P, A> type;
#endif
        type impl_;
    public:
        typedef typename type::value_type value_type;
        typedef typename type::iterator iterator;
        typedef typename type::const_iterator const_iterator;

        UnorderedMap() : impl_() { }

        iterator begin() { return impl_.begin(); }
        const_iterator begin() const { return impl_.begin(); }
        iterator end() { return impl_.end(); }
        const_iterator end() const { return impl_.end(); }
        std::pair<iterator, bool> insert(const std::pair<K, V>& kv)
        { return impl_.insert(kv); }
        iterator insert_unique(const std::pair<K, V>& kv)
        {
            std::pair<iterator, bool> ret(insert(kv));
            if (ret.second == false) gu_throw_fatal << "insert unique failed";
            return ret.first;
        }
        iterator find(const K& key) { return impl_.find(key); }
        const_iterator find(const K& key) const { return impl_.find(key); }
        iterator erase(iterator i) { return impl_.erase(i); }
        size_t size() const { return impl_.size(); }
        bool empty() const { return impl_.empty(); }
        void clear() { impl_.clear(); }
        void rehash(size_t n) { impl_.rehash(n); }
    };

    template <typename K, typename V, typename H = UnorderedHash<K> >
    class UnorderedMultimap
    {
#if defined(HAVE_BOOST_UNORDERED_MAP_HPP)
        typedef boost::unordered_multimap<K, V> type;
#elif defined(HAVE_UNORDERED_MAP)
        typedef std::unordered_multimap<K, V> type;
#elif defined(HAVE_TR1_UNORDERED_MAP)
        typedef std::tr1::unordered_multimap<K, V> type;
#endif
        type impl_;
    public:
        typedef typename type::value_type value_type;
        typedef typename type::iterator iterator;
        typedef typename type::const_iterator const_iterator;

        UnorderedMultimap() : impl_() { }
        void clear() { impl_.clear(); }

        iterator begin() { return impl_.begin(); }
        const_iterator begin() const { return impl_.begin(); }
        iterator end() { return impl_.end(); }
        const_iterator end() const { return impl_.end(); }
        iterator insert(const std::pair<K, V>& kv)
        { return impl_.insert(kv); }
        iterator find(const K& key) { return impl_.find(key); }
        const_iterator find(const K& key) const { return impl_.find(key); }
        std::pair<iterator, iterator>
        equal_range(const K& key) { return impl_.equal_range(key); }
        std::pair<const_iterator, const_iterator>
        equal_range(const K& key) const
        { return impl_.equal_range(key); }
        void erase(iterator i) { impl_.erase(i); }
        size_t size() const { return impl_.size(); }
        bool empty() const { return impl_.empty(); }
    };
}

#endif // GU_UNORDERED_HPP
