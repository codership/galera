#
# Copyright (C) 2025 Codership Oy <info@codership.com>
#

# -D_GLIBCXX_DEBUG makes linking with Boost program options
# library fail, so disable it for garbd.
remove_definitions(-D_GLIBCXX_DEBUG)

add_executable(garbd
  garb_config.cpp
  garb_logger.cpp
  garb_gcs.cpp
  garb_recv_loop.cpp
  garb_main.cpp
  )

target_include_directories(garbd
  PRIVATE
  ${PROJECT_SOURCE_DIR}/wsrep/src
  )

target_compile_definitions(garbd
  PRIVATE
  -DGALERA_VER="${GALERA_VERSION}"
  -DGALERA_REV="${GALERA_REVISION}"
  )

# TODO: Fix.
target_compile_options(garbd
  PRIVATE
  -Wno-conversion
  -Wno-unused-parameter
  )

target_link_libraries(garbd gcs4garb gcomm gcache
  ${Boost_PROGRAM_OPTIONS_LIBRARY})

install(TARGETS garbd DESTINATION bin)
if (NOT ${CMAKE_SYSTEM_NAME} MATCHES ".*BSD")
  install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/files/garb.cnf
    ${CMAKE_CURRENT_SOURCE_DIR}/files/garb.service
    ${CMAKE_CURRENT_SOURCE_DIR}/files/garb-systemd
    DESTINATION share)
  install(FILES
    ${PROJECT_SOURCE_DIR}/man/garbd.8
    DESTINATION man/man8)
endif()
