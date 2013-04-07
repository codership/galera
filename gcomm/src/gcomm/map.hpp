/*
 * Copyright (C) 2009-2012 Codership Oy <info@codership.com>
 */

/*!
 * @file map.hpp
 *
 * This file contains templates that are thin wrappers for std::map
 * and std::multimap with some extra functionality.
 */

#ifndef GCOMM_MAP_HPP
#define GCOMM_MAP_HPP

#include "gu_serialize.hpp"

#include <utility>
#include <iterator>
#include <map>

#include "gcomm/exception.hpp"
#include "gcomm/types.hpp"

namespace gcomm
{
    template<typename K, typename V, typename C>
    class MapBase
    {
        typedef C MapType;

    public:

        typedef typename MapType::iterator       iterator;
        typedef typename MapType::const_iterator const_iterator;
        typedef typename MapType::reverse_iterator reverse_iterator;
        typedef typename MapType::const_reverse_iterator const_reverse_iterator;
        typedef typename MapType::value_type     value_type;
        typedef typename MapType::const_reference const_reference;
        typedef typename MapType::key_type key_type;
        typedef typename MapType::mapped_type mapped_type;

    protected:

        MapType map_;
    public:

        MapBase() : map_() {}

        virtual ~MapBase() {}

        iterator begin()          { return map_.begin(); }

        iterator end()            { return map_.end();   }

        iterator find(const K& k) { return map_.find(k); }

        iterator find_checked(const K& k)
        {
            iterator ret = map_.find(k);
            if (ret == map_.end())
            {
                gu_throw_fatal << "element " << k << " not found";
            }
            return ret;
        }

        iterator lower_bound(const K& k) { return map_.lower_bound(k); }

        const_iterator begin()          const { return map_.begin(); }

        const_iterator end()            const { return map_.end();   }

        const_reverse_iterator rbegin()         const { return map_.rbegin(); }

        const_reverse_iterator rend()           const { return map_.rend(); }

        const_iterator find(const K& k) const { return map_.find(k); }

        const_iterator find_checked(const K& k) const
        {
            const_iterator ret = map_.find(k);
            if (ret == map_.end())
            {
                gu_throw_fatal << "element " << k << " not found";
            }
            return ret;
        }

        mapped_type& operator[](const key_type& k) { return map_[k]; }

        void erase(iterator i) { map_.erase(i); }

        void erase(iterator i, iterator j) { map_.erase(i, j); }

        void erase(const K& k) { map_.erase(k); }

        void clear()           { map_.clear(); }

        size_t size() const    { return map_.size(); }

        bool empty() const     { return map_.empty(); }

        size_t serialize(gu::byte_t* const buf,
                         size_t  const buflen,
                         size_t        offset) const
        {
            gu_trace(offset = gu::serialize4(
                         static_cast<uint32_t>(size()), buf, buflen, offset));
            for (const_iterator i = map_.begin(); i != map_.end(); ++i)
            {
                gu_trace(offset = key(i).serialize(buf, buflen, offset));
                gu_trace(offset = value(i).serialize(buf, buflen, offset));
            }
            return offset;
        }

        size_t unserialize(const gu::byte_t* buf,
                           size_t const  buflen,
                           size_t        offset)
        {
            uint32_t len;
            // Clear map in case this object is reused
            map_.clear();

            gu_trace(offset = gu::unserialize4(buf, buflen, offset, len));;

            for (uint32_t i = 0; i < len; ++i)
            {
                K k;
                V v;
                gu_trace(offset = k.unserialize(buf, buflen, offset));
                gu_trace(offset = v.unserialize(buf, buflen, offset));
                if (map_.insert(std::make_pair(k, v)).second == false)
                {
                    gu_throw_fatal << "Failed to unserialize map";
                }
            }
            return offset;
        }

        size_t serial_size() const
        {
            return sizeof(uint32_t) + size()*(K::serial_size() + V::serial_size());
        }

        bool operator==(const MapBase& other) const
        {
            return (map_ == other.map_);
        }

        bool operator!=(const MapBase& other) const
        {
            return !(map_ == other.map_);
        }

        static const K& key(const_iterator i)
        {
            return i->first;
        }

        static const K& key(iterator i)
        {
            return i->first;
        }

        static const V& value(const_iterator i)
        {
            return i->second;
        }

        static V& value(iterator i)
        {
            return i->second;
        }

        static const K& key(const value_type& vt)
        {
            return vt.first;
        }

        static V& value(value_type& vt)
        {
            return vt.second;
        }

        static const V& value(const value_type& vt)
        {
            return vt.second;
        }
    };

    // @todo For some reason map key must be declared in gcomm namespace
    //       in order this to work. Find out the reason why and fix.
    template <typename K, typename V>
    std::ostream& operator<<(std::ostream& os, const std::pair<K, V>& p)
    {
        return (os << "\t" << p.first << "," << p.second << "\n");
    }

    template <typename K, typename V, typename C>
    std::ostream& operator<<(std::ostream& os, const MapBase<K, V, C>& map)
    {
        std::copy(map.begin(), map.end(),
                  std::ostream_iterator<const std::pair<const K, V> >(os, ""));
        return os;
    }


    template <typename K, typename V, typename C = std::map<K, V> >
    class Map : public MapBase<K, V, C>
    {
    public:
        typedef typename MapBase<K, V, C>::iterator iterator;
        std::pair<iterator, bool> insert(const std::pair<K, V>& p)
        {
            return MapBase<K, V, C>::map_.insert(p);
        }

        template <class InputIterator>
        void insert(InputIterator first, InputIterator last)
        {
            MapBase<K, V, C>::map_.insert(first, last);
        }

        iterator insert_unique(const typename MapBase<K, V, C>::value_type& p)
        {
            std::pair<iterator, bool> ret = MapBase<K, V, C>::map_.insert(p);
            if (false == ret.second)
            {
                gu_throw_fatal << "duplicate entry "
                               << "key=" << MapBase<K, V, C>::key(p) << " "
                               << "value=" << MapBase<K, V, C>::value(p) << " "
                               << "map=" << *this;
            }
            return ret.first;
        }

    };




    template <typename K, typename V, typename C = std::multimap<K, V> >
    class MultiMap : public MapBase<K, V, C>
    {
    public:
        typedef typename MapBase<K, V, C>::iterator iterator;
        typedef typename MapBase<K, V, C>::const_iterator const_iterator;
        typedef typename MapBase<K, V, C>::value_type value_type;
        typedef typename MapBase<K, V, C>::const_reference const_reference;

        iterator insert(const std::pair<K, V>& p)
        {
            return MapBase<K, V, C>::map_.insert(p);
        }

        iterator insert(iterator position, const value_type& vt)
        {
            return MapBase<K, V, C>::map_.insert(position, vt);
        }

        std::pair<const_iterator, const_iterator> equal_range(const K& k) const
        {
            return MapBase<K, V, C>::map_.equal_range(k);
        }
    };
}
#endif /* GCOMM_MAP_HPP */
