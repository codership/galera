#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

#
# Note: Files under ../src are inside wsrep-API submodule, so
# these tests won't compile unless the submodule has been
# initialized/updated.
#

add_executable(wsrep_test
  wsrep_loader_test.c
  ../src/wsrep_loader.c
  ../src/wsrep_dummy.c)
target_compile_definitions(wsrep_test
  PRIVATE
  -DWSREP_PROVIDER="${CMAKE_BINARY_DIR}/libgalera_smm.so"
  -DGALERA_VERSION="${GALERA_VERSION}"
  -DGALERA_GIT_REVISION="${GALERA_GIT_REVISION}")
target_link_libraries(wsrep_test dl)
add_test(NAME wsrep_test COMMAND wsrep_test)
