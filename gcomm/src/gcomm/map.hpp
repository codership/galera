/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef GCOMM_MAP_HPP
#define GCOMM_MAP_HPP

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

        MapType map;
    public:
        
        MapBase() : map() {}
        
        virtual ~MapBase() {}
        
        iterator begin()          { return map.begin(); }
        
        iterator end()            { return map.end();   }

        iterator find(const K& k) { return map.find(k); }
        
        iterator find_checked(const K& k)
        {
            iterator ret = map.find(k);
            if (ret == map.end())
            {
                gcomm_throw_fatal << "element " << k << " not found";
            }
            return ret;
        }
        
        iterator lower_bound(const K& k) { return map.lower_bound(k); }
        
        const_iterator begin()          const { return map.begin(); }

        const_iterator end()            const { return map.end();   }

        const_reverse_iterator rbegin()         const { return map.rbegin(); }
        
        const_reverse_iterator rend()           const { return map.rend(); }
        
        const_iterator find(const K& k) const { return map.find(k); }
        
        const_iterator find_checked(const K& k) const 
        {
            const_iterator ret = map.find(k);
            if (ret == map.end())
            {
                gcomm_throw_fatal << "element " << k << " not found";
            }
            return ret;
        }
        
        mapped_type& operator[](const key_type& k) { return map[k]; }
    
        void erase(iterator i) { map.erase(i); }

        void erase(iterator i, iterator j) { map.erase(i, j); }

        void erase(const K& k) { map.erase(k); }

        void clear()           { map.clear(); }
    
        size_t size() const    { return map.size(); }

        bool empty() const     { return map.empty(); }

        size_t serialize(gu::byte_t* const buf,
                         size_t  const buflen, 
                         size_t        offset) const
            throw (gu::Exception)
        {
            gu_trace(offset = gcomm::serialize(
                         static_cast<uint32_t>(size()), buf, buflen, offset));
            for (const_iterator i = map.begin(); i != map.end(); ++i)
            {
                gu_trace(offset = get_key(i).serialize(buf, buflen, offset));
                gu_trace(offset = get_value(i).serialize(buf, buflen, offset));
            }
            return offset;
        }
        
        size_t unserialize(const gu::byte_t* buf, 
                           size_t const  buflen, 
                           size_t        offset)
            throw (gu::Exception)
        {
            uint32_t len;
            // Clear map in case this object is reused
            map.clear();
            
            gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &len));;
            
            for (uint32_t i = 0; i < len; ++i)
            {
                K k;
                V v;
                gu_trace(offset = k.unserialize(buf, buflen, offset));
                gu_trace(offset = v.unserialize(buf, buflen, offset));
                if (map.insert(std::make_pair(k, v)).second == false)
                {
                    gcomm_throw_fatal << "Failed to unserialize map";
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
            return (map == other.map);
        }

        bool operator!=(const MapBase& other) const
        {
            return !(map == other.map);
        }
        
        static const K& get_key(const_iterator i)
        {
            return i->first;
        }

        static const K& get_key(iterator i)
        {
            return i->first;
        }

        static const V& get_value(const_iterator i)
        {
            return i->second;
        }

        static V& get_value(iterator i)
        {
            return i->second;
        }

        static const K& get_key(const value_type& vt)
        {
            return vt.first;
        }
        
        static V& get_value(value_type& vt)
        {
            return vt.second;
        }

        static const V& get_value(const value_type& vt)
        {
            return vt.second;
        }
    };
    
    // @todo For some reason map key must be declared in gcomm namespace 
    //       in order this to work. Find out the reason why and fix.
    template <typename K, typename V>
    std::ostream& operator<<(std::ostream& os, const std::pair<K, V>& p)
    {
        return (os << p.first << "," << p.second);
    }
    
    template <typename K, typename V, typename C>
    std::ostream& operator<<(std::ostream& os, const MapBase<K, V, C>& map)
    {
        std::copy(map.begin(), map.end(),
                  std::ostream_iterator<const std::pair<const K, V> >(os, " "));
        return os;
    }
    

    template <typename K, typename V, typename C = std::map<K, V> >
    class Map : public MapBase<K, V, C>
    {
    public:
        typedef typename MapBase<K, V, C>::iterator iterator;
        std::pair<iterator, bool> insert(const std::pair<K, V>& p)
        {
            return MapBase<K, V, C>::map.insert(p);
        }
        
        iterator insert_checked(const typename MapBase<K, V, C>::value_type& p)
        {
            std::pair<iterator, bool> ret = MapBase<K, V, C>::map.insert(p);
            if (false == ret.second)
            {
                gcomm_throw_fatal << "duplicate entry " 
                                  << "key=" << get_key(p) << " "
                                  << "value=" << get_value(p) << " "
                                  << "map=" << *this;
            }
            return ret.first;
        }

        iterator insert_unique(const typename MapBase<K, V, C>::value_type& vt)
        {
            return insert_checked(vt);
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
            return MapBase<K, V, C>::map.insert(p);
        }
        
        iterator insert(iterator position, const value_type& vt)
        {
            return MapBase<K, V, C>::map.insert(position, vt);
        }
        
        std::pair<const_iterator, const_iterator> equal_range(const K& k) const
        {
            return MapBase<K, V, C>::map.equal_range(k);
        }
    };

    template <typename K, typename V>
    const K& get_key(const typename Map<K, V>::const_iterator i)
    {
        return i->first;
    }

    template <typename K, typename V>
    const K& get_key(const typename Map<K, V>::value_type& vt)
    {
        return vt.first;
    }

    template <typename K, typename V>
    const V& get_value(const typename Map<K, V>::value_type& vt)
    {
        return vt.second;
    }

}
#endif /* GCOMM_MAP_HPP */
