=========================================
Installing Galera Cluster for Percona XtraDB from Source
=========================================
.. _'XtraDB Source Installation'

If you run a machine that does not support Debian- or RPM-based binary installations, you can install Galera Cluster for Percona XtraDB by compiling from source.


.. note:: This tutorial omits Percona XtraDB authentication options for brevity.

-----------------------------------------
Build Dependencies
-----------------------------------------
.. _`Build Dependencies`:

In order to install Galera Cluster for Percona XtraDB from source, you must first install the build dependencies on your server.

- cmake
- gcc
- gcc-c++
- libaio
- libaio-devel
- automake
- autoconf
- bzr
- bison
- libtool
- ncurses5-devel
- boost

Once you have these installed, you can begin compiling Galera Cluster.

------------------------------------------
Building Galera Cluster for Percona XtraDB
------------------------------------------
.. _`Build Galera XtraDB`:

To build Galera Cluster, complete the following steps:

1. Download and extract Galera Cluster for Percona XtraDB from `Percona <http://www.percona.com/downloads/Percona-XtraDB-Cluster>`_::

	$ wget http://www.percona.com/downloads/Percona-XtraDB-Cluster/version_nbr/source/percona-xtradb-cluster-galera.tar.gz
	$ tar zxf percona-xtradb-cluster-galera.tar.gz

2. Build the Percona server::

	$ cmake -DWITH_WSREP-1 \
		-DWITH_INNODB_DISALLOW_WRITES=1
	$ make

Galera Cluster for Percona XtraDB is now installed on your server.

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Updating System Tables
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`Update System Tables`:

If you chose to overwrite an existing installation of the Percona XtraDB server, you must also upgrade the system tables to the new system.

To upgrade the system tables, after you start the XtraDB server run the following from the command-line::

	$ xtradb_upgrade

If this command generates any errors, check the Percona XtraDB Documentation for more information related to the error messages.  The errors it generates are typically not critical and you can usually ignore them, unless they involve specific functionality that your system requires.

