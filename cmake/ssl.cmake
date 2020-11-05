#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

if (NOT GALERA_WITH_SSL)
  return()
endif()

# Make sure not to build static version with system SSL libraries.
if (GALERA_STATIC)
  if (NOT OPENSSL_ROOT_DIR)
    message(FATAL_ERROR "Specify OPENSSL_ROOT_DIR for GALERA_STATIC")
  endif()
endif()

if (CMAKE_VERSION VERSION_GREATER "3.1")
  set(OPENSSL_USE_STATIC_LIBS ${GALERA_STATIC})
  find_package(OpenSSL 1.0)
  if (NOT OPENSSL_FOUND)
    message(FATAL_ERROR "OpenSSL libraries could not be found")
  endif()
  message(STATUS "${OPENSSL_LIBRARIES}")
  set(GALERA_SSL_LIBS ${OPENSSL_LIBRARIES})
  message(STATUS "GALERA_SSL_LIBS: ${GALERA_SSL_LIBS}")
  return()
endif()

# Fall back for older versions of CMake which don't have
# OPENSSL_USE_STATIC_LIBS hint,
if (GALERA_STATIC)
  list(APPEND CMAKE_REQUIRED_INCLUDES SYSTEM ${OPENSSL_ROOT_DIR}/include)
  check_include_file(openssl/ssl.h HAVE_SSL_H)
  if (NOT HAVE_SSL_H)
    message(FATAL_ERROR
      "Header openssl/ssl.h not found from path "
      "${OPENSSL_ROOT_DIR}/include")
  endif()
  include_directories(BEFORE SYSTEM ${OPENSSL_ROOT_DIR}/include)
  find_library(HAVE_STATIC_SSL libssl.a
    PATHS ${OPENSSL_ROOT_DIR}/lib)
  find_library(HAVE_STATIC_CRYPTO libcrypto.a
    PATHS ${OPENSSL_ROOT_DIR}/lib)
  if (NOT HAVE_STATIC_SSL)
    message(FATAL_ERROR
      "Static SSL library not found from ${OPENSSL_ROOT_DIR}/lib")
  endif()
  if (NOT HAVE_STATIC_CRYPTO)
    message(FATAL_ERROR
      "Static crypto library not found from ${OPENSSL_ROOT_DIR}/lib")
  endif()
  set(GALERA_SSL_LIBS
    ${OPENSSL_ROOT_DIR}/lib/libssl.a
    ${OPENSSL_ROOT_DIR}/lib/libcrypto.a dl)
elseif (GALERA_WITH_SSL)
  check_include_file(openssl/ssl.h HAVE_SSL_H)
  if (NOT HAVE_SSL_H)
    message(FATAL_ERROR
      "Header openssl/ssl.h not found from system include path")
  endif()
  find_library(HAVE_SSL_LIB ssl)
  if (NOT HAVE_SSL_LIB)
    message(FATAL_ERROR "SSL library not found from system libaray path")
  else()
    set(GALERA_SSL_LIBS ssl crypto)
  endif()
endif()

message(STATUS "GALERA_SSL_LIBS: ${GALERA_SSL_LIBS}")

unset(HAVE_SSL_LIB CACHE)
unset(HAVE_SSL_H CACHE)
