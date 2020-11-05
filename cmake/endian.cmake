#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#
# Checks for byte order and byteswap headers.
#

check_include_file(endian.h HAVE_ENDIAN_H)
check_include_file(sys/endian.h HAVE_SYS_ENDIAN_H)
check_include_file(sys/byteorder.h HAVE_SYS_BYTEORDER_H)

if (HAVE_ENDIAN_H)
  add_definitions(-DHAVE_ENDIAN_H)
elseif (HAVE_SYS_ENDIAN_H)
  add_definitions(-DHAVE_SYS_ENDIAN_H)
elseif(HAVE_SYS_BYTEORDER_H)
  add_definitions(-DHAVE_SYS_BYTEORDER_H)
endif()

check_include_file(byteswap.h HAVE_BYTESWAP_H)
if (HAVE_BYTESWAP_H)
  add_definitions(-DHAVE_BYTESWAP_H)
endif()
