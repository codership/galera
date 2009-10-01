/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file inst_map.hpp Common template for UUID to class map
 */

#ifndef INST_MAP_HPP
#define INST_MAP_HPP

#include "gcomm/common.hpp"
#include "gcomm/uuid.hpp"

#include <map>

/* Forward declarations */
namespace gcomm
{
    template<class T> class InstMap;
}


/*!
 * @brief Template for mapping between UUID and class.
 *
 * 
 */
template<class T> class gcomm::InstMap
{
    typedef std::map<const UUID, T> MType; /*!< Internal map representation */

public:

    typedef typename MType::iterator       iterator;       /*!< Iterator */
    typedef typename MType::const_iterator const_iterator; /*!< Const iterator*/

private:

    MType map; /*!< Map */

public:    

    /*!
     * @brief Default constructor
     */
    InstMap() : map() {}

    /*!
     * @brief Destructor
     */
    virtual ~InstMap() {}

    /*!
     *
     */
    iterator begin() { return map.begin(); }
    
    /*!
     *
     */
    const_iterator begin() const { return map.begin(); }

    /*!
     *
     */
    iterator end() { return map.end(); }

    /*!
     *
     */
    const_iterator end() const { return map.end(); }

    /*!
     *
     */
    iterator find(const UUID& uuid) { return map.find(uuid); }

    /*!
     *
     */
    const_iterator find(const UUID& uuid) const { return map.find(uuid); }

    /*!
     *
     */
    std::pair<iterator, bool> insert(const std::pair<const UUID, T>& p)
    {
        return map.insert(p);
    }

    /*!
     *
     */
    void erase(iterator i) { map.erase(i); }

    /*!
     *
     */
    void clear() { map.clear(); }

    /*!
     *
     */
    size_t length() const { return map.size(); }
    
    /*!
     *
     */
    size_t read(const byte_t* buf, const size_t buflen, const size_t offset)
        throw (gu::Exception)
    {
        size_t off = offset;
        uint32_t len;
        // Clear map in case this object is reused
        map.clear();

        gu_trace (off = gcomm::read(buf, buflen, off, &len));

        for (uint32_t i = 0; i < len; ++i)
        {
            UUID uuid;
            T    t;

            gu_trace (off = uuid.read (buf, buflen, off));
            gu_trace (off = t.read    (buf, buflen, off));

            if (map.insert(std::make_pair(uuid, t)).second == false)
            {
                gcomm_throw_fatal << "Failed to insert " << uuid.to_string()
                                  << " in the map";
            }
        }

        return off;
    }

    /*!
     *
     */    
    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const
        throw (gu::Exception)
    {
        size_t   off = offset;
        uint32_t len(static_cast<uint32_t>(length()));

        gu_trace (off = gcomm::write(len, buf, buflen, off));

        typename MType::const_iterator i;

        for (i = map.begin(); i != map.end(); ++i)
        {
            gu_trace (off = i->first.write (buf, buflen, off));
            gu_trace (off = i->second.write(buf, buflen, off));
        }

        return off;
    }
    
    /*!
     *
     */
    size_t size() const
    {
        //            length
        return sizeof(uint32_t) + length() * (UUID::size() + T::size());
    }
    
    /*!
     *
     */
    std::string to_string() const
    {
        std::string ret;
        
        for (const_iterator i = map.begin(); i != map.end(); ++i)
        {
            const_iterator i_next = i;
            ++i_next;
            ret += get_uuid(i).to_string() + ": ";
            ret += get_instance(i).to_string();
            if (i_next != map.end())
            {
                ret += ",";
            }
        }
        return ret;
    }

    /*!
     *
     */
    bool operator==(const InstMap& other) const
    {
        return map == other.map;
    }

    /*!
     *
     */
    static const UUID& get_uuid(iterator i)
    {
        return i->first;
    }

    /*!
     *
     */
    static const UUID& get_uuid(const_iterator i)
    {
        return i->first;
    }

    /*!
     *
     */
    static T& get_instance(iterator i)
    {
        return i->second;
    }

    /*!
     *
     */
    static const T& get_instance(const_iterator i)
    {
        return i->second;
    }

    
};


#endif // INST_MAP_HPP
