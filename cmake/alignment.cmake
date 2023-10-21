#
# Copyright (C) 2023 Codership Oy <info@codership.com>
#
# Disable backward compatibility unit tests running old unaligned data format
# on non-x86 platforms because it may cause bus error
#
if (NOT ${CMAKE_SYSTEM_PROCESSOR} MATCHES "(x86_64|AMD64|i[3-6]86)")
  add_definitions(-DGALERA_ONLY_ALIGNED)
endif()
