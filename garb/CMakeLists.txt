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

# Make the install destination for garbd binary configurable
if(NOT DEFINED INSTALL_GARBD)
  set(INSTALL_GARBD "bin" CACHE STRING "path to install garbd binary to")
endif()

install(TARGETS garbd
  DESTINATION ${INSTALL_GARBD})

if (NOT ${CMAKE_SYSTEM_NAME} MATCHES ".*BSD")

  # Make the install destination for garbd-systemd wrapper script configurable
  if(NOT DEFINED INSTALL_GARBD-SYSTEMD)
    set(INSTALL_GARBD-SYSTEMD "share" CACHE STRING "path to install garbd-systemd wrapper script to")
  endif()

  install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/files/garb-systemd
    DESTINATION ${INSTALL_GARBD-SYSTEMD})

  # Make the install destination for garbd configuration file configurable
  if(NOT DEFINED INSTALL_CONFIGURATION)
    set(INSTALL_CONFIGURATION "share" CACHE STRING "path to install garbd configuration to")
  endif()

  install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/files/garb.cnf
    DESTINATION ${INSTALL_CONFIGURATION}
    RENAME garb)

  # Make the install destination for garbd systemd service file configurable
  if(NOT DEFINED INSTALL_SYSTEMD_SERVICE)
    set(INSTALL_SYSTEMD_SERVICE "share" CACHE STRING "path to install garbd Systemd service to")
  endif()

  install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/files/garb.service
    DESTINATION ${INSTALL_SYSTEMD_SERVICE})

  # Make the install destination for manpage configurable
  if(NOT DEFINED INSTALL_MANPAGE)
    set(INSTALL_MANPAGE "man/man8" CACHE STRING "path to install manpage to")
  endif()

  install(FILES
    ${PROJECT_SOURCE_DIR}/man/garbd.8
    DESTINATION ${INSTALL_MANPAGE})

endif()
