#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

# Even with C++11 we still prefer boost::shared_ptr until
# issues with compiling mixed ASIO/boost/std::shared_ptr
# code have been sorted out.
list(APPEND CMAKE_REQUIRED_INCLUDES ${Boost_INCLUDE_DIR})
check_include_file_cxx(boost/shared_ptr.hpp HAVE_BOOST_SHARED_PTR_HPP)
if (HAVE_BOOST_SHARED_PTR_HPP)
  add_definitions(-DHAVE_BOOST_SHARED_PTR_HPP)
  return()
endif()

if (NOT CMAKE_CXX_STANDARD EQUAL 98)
  add_definitions(-DHAVE_STD_SHARED_PTR)
  return()
endif()

message(FATAL_ERROR "No suitable shared_ptr library found.")
