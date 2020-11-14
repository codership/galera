#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

if (NOT GALERA_WITH_SSL)
  return()
endif()

#
# Helper macros to check Ecdh features.
#

macro(CHECK_ECDH_AUTO)
  check_cxx_source_compiles(
    "
#include <openssl/ssl.h>
int main() { SSL_CTX* ctx=NULL; return !SSL_CTX_set_ecdh_auto(ctx, 1); }
" ECDH_AUTO_OK)
  if (ECDH_AUTO_OK)
    add_definitions(-DOPENSSL_HAS_SET_ECDH_AUTO)
  endif()
endmacro()

macro(CHECK_TMP_ECDH)
  check_cxx_source_compiles(
    "
#include <openssl/ssl.h>
int main() { SSL_CTX* ctx=NULL; EC_KEY* ecdh=NULL; return !SSL_CTX_set_tmp_ecdh(
ctx,ecdh); }
" ECDH_TMP_OK)
  if (ECDH_TMP_OK)
    add_definitions(-DOPENSSL_HAS_SET_TMP_ECDH)
  endif()
endmacro()

macro(CHECK_ECDH)
  CHECK_ECDH_AUTO()
  if (NOT ECDH_AUTO_OK)
    CHECK_TMP_ECDH()
  endif()
endmacro()

# Make sure not to build static version with system SSL libraries.
if (GALERA_STATIC)
  if (NOT OPENSSL_ROOT_DIR)
    message(FATAL_ERROR "Specify OPENSSL_ROOT_DIR for GALERA_STATIC")
  endif()
endif()

# OPENSSL_USE_STATIC_LIBS was introduced in CMake 3.3.2. For earlier versions
# there is a fallback code below.
if (CMAKE_VERSION VERSION_GREATER "3.3.1")
  set(OPENSSL_USE_STATIC_LIBS ${GALERA_STATIC})
  find_package(OpenSSL 1.0)
  if (NOT OPENSSL_FOUND)
    message(FATAL_ERROR "OpenSSL libraries could not be found")
  endif()
  message(STATUS "${OPENSSL_LIBRARIES}")
  set(GALERA_SSL_LIBS ${OPENSSL_LIBRARIES})
  if (GALERA_STATIC)
    list(APPEND GALERA_SSL_LIBS dl pthread)
  endif()
  message(STATUS "GALERA_SSL_LIBS: ${GALERA_SSL_LIBS}")
  list(APPEND CMAKE_REQUIRED_LIBRARIES ${GALERA_SSL_LIBS})
  CHECK_ECDH()
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
    ${OPENSSL_ROOT_DIR}/lib/libcrypto.a dl pthread)
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

list(APPEND CMAKE_REQUIRED_LIBRARIES ${GALERA_SSL_LIBS})
CHECK_ECDH()
message(STATUS "GALERA_SSL_LIBS: ${GALERA_SSL_LIBS}")

unset(HAVE_SSL_LIB CACHE)
unset(HAVE_SSL_H CACHE)
