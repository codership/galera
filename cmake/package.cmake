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

include(CPack)
