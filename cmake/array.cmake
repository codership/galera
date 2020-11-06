#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

if (CMAKE_CXX_STANDARD EQUAL 98)
  add_definitions(-DHAVE_BOOST_ARRAY_HPP)
else()
  add_definitions(-DHAVE_STD_ARRAY)
endif()
