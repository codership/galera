#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#
#
# Unordered map implementation selection in order of
# precedence
# - std::unordered_map
# - std::tr1::unordered_map
# - boost::unordered_map
#

if (NOT CMAKE_CXX_STANDARD EQUAL 98)
  add_definitions(-DHAVE_STD_UNORDERED_MAP)
  return()
endif()

check_include_file_cxx(tr1/unordered_map HAVE_TR1_UNORDERED_MAP)
if (HAVE_TR1_UNORDERED_MAP)
  check_cxx_source_compiles("
#include <tr1/unordered_map>
int main() { std::tr1::unordered_map<int, int> m; }
" STD_TR1_UNORDERED_MAP_COMPILES)
  if (STD_TR1_UNORDERED_MAP_COMPILES)
    add_definitions(-DHAVE_TR1_UNORDERED_MAP)
    return()
  endif()
endif()

check_include_file_cxx(boost/unordered_map.hpp HAVE_BOOST_UNORDERED_MAP_HPP)
if (HAVE_BOOST_UNORDERED_MAP_HPP)
  check_cxx_source_compiles("
#include <boost/unordered_map.hpp>
int main() { boost::unordered_map<int, int> m; }
" BOOST_UNORDERED_MAP_COMPILES)
  if (BOOST_UNORDERED_MAP_COMPILES)
    add_definitions(-DHAVE_BOOST_UNORDERED_MAP_HPP)
    return()
  endif()
endif()

message(FATAL_ERROR "Suitable unordered_map implementation not found")
