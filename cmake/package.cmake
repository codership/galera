#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

#
# For historical reasons, packaging version numbers are shifted so that the
# wsrep-api version becomes package major version, galera major version
# becomes package minor version and galera minor version + extra becomes
# patch version.
#

set(CPACK_PACKAGE_VERSION_MAJOR ${GALERA_VERSION_WSREP_API})
set(CPACK_PACKAGE_VERSION_MINOR ${GALERA_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_PATCH ${GALERA_VERSION_MINOR}${GALERA_VERSION_EXTRA})

# Source
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${PROJECT_NAME}-${GALERA_VERSION_WSREP_API}.${GALERA_VERSION_MAJOR}.${GALERA_VERSION_MINOR}${GALERA_VERSION_EXTRA}${GALERA_REVISION}")
set(CPACK_SOURCE_IGNORE_FILES
  "\\\\.git/"
  "\\\\.gitignore"
  "\\\\.gitmodules"
  "\\\\.travis\\\\.yml"
  "\\\\.bzrignore"
  "/tests/test_causal"
  "/tests/test_cppcheck"
  "/tests/test_dbt2"
  "/tests/test_dots"
  "/tests/test_drupal"
  "/tests/test_insert"
  "/tests/test_memory"
  "/tests/test_mtr"
  "/tests/test_overhead"
  "/tests/test_pc_recovery"
  "/tests/test_seesaw"
  "/tests/test_segmentation"
  "/tests/test_sqlgen"
  "/tests/test_startstop"
  "/tests/test_stopcont"
  "/tests/test_upgrade"
  "/tests/regressions/"
  "/tests/tap/"
  "/tests/t/"
  "/scripts/openrep/"
  "/scripts/mysql/"
  "/scripts/packages/freebsd/"
  "/scripts/packages/debian/"
  "/CMakeFiles/"
  "CMakeCache\\\\.txt"
  "/cmake_install\\\\.cmake"
  "/CTestTestfile\\\\.cmake"
  "/CPackSourceConfig\\\\.cmake"
  "/CPackConfig\\\\.cmake"
  "/_CPack_Packages/"
  "Makefile"
  "\\\\.gz")

# Binary

set(CPACK_GENERATOR "TGZ")
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${GALERA_VERSION_WSREP_API}.${GALERA_VERSION_MAJOR}.${GALERA_VERSION_MINOR}${GALERA_VERSION_EXTRA}${GALERA_REVISION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

include(CPack)
