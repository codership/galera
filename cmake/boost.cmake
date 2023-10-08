#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

set(Boost_USE_MULTITHREAD ON)
set(Boost_USE_STATIC_LIBS ${GALERA_STATIC})
find_package(Boost 1.41 COMPONENTS program_options)
if (NOT Boost_FOUND)
  if (Boost_USE_STATIC_LIBS)
    message(ERROR
      "Boost package detection failed for static libraries, "
      "make sure that static boost libraries are installed")
  endif()
  message(FATAL_ERROR "Could not find BOOST components")
else()
  include_directories(${Boost_INCLUDE_DIRS})
  message(STATUS ${Boost_PROGRAM_OPTIONS_LIBRARIES})
endif()

# Use nanosecond time precision
add_definitions(-DBOOST_DATE_TIME_POSIX_TIME_STD_CONFIG=1)
# This is to suppress deprecation message
#
#     The practice of declaring the Bind placeholders (_1, _2, ...)
#     in the global namespace is deprecated. Please use
#     <boost/bind/bind.hpp> + using namespace boost::placeholders,
#     or define BOOST_BIND_GLOBAL_PLACEHOLDERS to retain the current
#     behavior.
#
# Using <boost/bind/bind.hpp> + boost:placeholders worked on all
# platforms, except on CentOS/RHEL 7 where the boost version is
# 1.53.0. Consider removing -DBOOST_BIND_GLOBAL_PLACEHOLDERS
# and using boost::placeholders after CentOS/RHEL builds are
# not needed anymore.
add_definitions(-DBOOST_BIND_GLOBAL_PLACEHOLDERS=1)
