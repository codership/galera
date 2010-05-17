
#ifndef GU_UNORDERED_HPP
#define GU_UNORDERED_HPP

#if defined(HAVE_BOOST_UNORDERED_MAP)
#include <boost/unordered_map.hpp>
#elif defined(HAVE_TR1_UNORDERED_MAP)
#include <tr1/unordered_map>
#else
#error "no unordered map available"
#endif


namespace gu
{
  template <typename K, typename V, typename H>
  class unordered_map
  {
  public:
#if defined(HAVE_BOOST_UNORDERED_MAP_HPP)
    typedef boost::unordered_map<K, V, H> type;
#elif defined(HAVE_TR1_UNORDERED_MAP)
    typedef std::tr1::unordered_map<K, V, H> type;
#endif
  };

  template <typename K, typename V>
  class unordered_multimap
  {
  public:
#if defined(HAVE_BOOST_UNORDERED_MAP_HPP)
    typedef boost::unordered_multimap<K, V> type;
#elif defined(HAVE_TR1_UNORDERED_MAP)
    typedef std::tr1::unordered_multimap<K, V> type;
#endif
  };
}

#endif // GU_UNORDERED_HPP
