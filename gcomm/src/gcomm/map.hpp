
#ifndef GCOMM_MAP_HPP
#define GCOMM_MAP_HPP

#include <utility>

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

    protected:

        MapType map;

    public:

        MapBase() : map() {}

        virtual ~MapBase() {}

        iterator begin()          { return map.begin(); }

        iterator end()            { return map.end();   }

        iterator find(const K& k) { return map.find(k); }

        const_iterator begin()          const { return map.begin(); }

        const_iterator end()            const { return map.end();   }

        const_iterator find(const K& k) const { return map.find(k); }
    
        void erase(iterator i) { map.erase(i); }

        void erase(const K& k) { map.erase(k); }

        void clear()           { map.clear(); }
    
        size_t size() const    { return map.size(); }

        size_t serialize(byte_t*      buf,
                         const size_t buflen, 
                         const size_t offset) const
        {
            size_t   off(offset);

            if ((off = gcomm::write(static_cast<uint32_t>(size()), 
                                    buf, buflen, off)) == 0) return 0;
            
//            typename MapType::const_iterator i;
            const_iterator i;

            for (i = map.begin(); i != map.end(); ++i)
            {
                if ((off = i->first.write (buf, buflen, off)) == 0) return 0;
                if ((off = i->second.write(buf, buflen, off)) == 0) return 0;
            }
            return off;
        }
    
        size_t unserialize(const byte_t* buf, const size_t buflen, 
                           const size_t offset)
        {
            size_t   off = offset;
            uint32_t len;

            // Clear map in case this object is reused
            map.clear();

            if ((off = gcomm::read(buf, buflen, off, &len)) == 0) return 0;

            for (uint32_t i = 0; i < len; ++i)
            {
                K uuid;
                V t;

                if ((off = uuid.read(buf, buflen, off)) == 0) return 0;
                if ((off = t.read   (buf, buflen, off)) == 0) return 0;

                if (map.insert(std::make_pair(uuid, t)).second == false)
                {
                    gcomm_throw_fatal << "Failed to unserialize map";
                }
            }
            return off;
        }

        size_t serial_size() const
        {
            return sizeof(uint32_t) + size()*(K::size() + V::size());
        }

        bool operator==(const MapBase& other) const
        {
            return map == other.map;
        }
        
        static const K& get_key(const_iterator i)
        {
            return i->first;
        }

        static K& get_key(iterator i)
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

    };


    template<typename K, typename V, typename C>
    class Map : public MapBase<K, V, C>
    {
    public:
        typedef typename MapBase<K, V, C>::iterator iterator;
        std::pair<iterator, bool> insert(const std::pair<K, V>& p)
        {
            return MapBase<K, V, C>::map.insert(p);
        }
    };

    template<typename K, typename V, typename C>
    class MultiMap : public MapBase<K, V, C>
    {
    public:
        typedef typename MapBase<K, V, C>::const_iterator const_iterator;
        void insert(const std::pair<K, V>& p)
        {
            MapBase<K, V, C>::map.insert(p);
        }

        std::pair<const_iterator, const_iterator> equal_range(const K& k) const
        {
            return MapBase<K, V, C>::map.equal_range(k);
        }
    };


}
#endif /* GCOMM_MAP_HPP */
