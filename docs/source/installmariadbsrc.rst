=========================================
Source Installation
=========================================
.. _'MariaDB-Source Installation'

If you run a machine that does not support Debian- or RPM-based binary installations, you can install Galera Cluster for MariaDB by compiling from source.

.. note:: This tutorial omits MariaDB authentication options for brevity.

-----------------------------------------
Build Dependencies
-----------------------------------------
.. _`Build Dependencies`:

In order to install Galera Cluster for MariaDB from source, you must first install the build dependencies on your server.

- bzr > 2.0
- GNU toolchain, gcc/g++, version 4.4 or later
- libtool, version 1.5.24 or later
- bison, version 2.0, for MariaDB 5.5
- libncurses
- zlib-dev
- Boost libraries, version 1.41 or later
- `Check <http://check.sourceforge.net/>`_
- `Scons <http://www.scons.org/>`_

Once you have these installed, you can begin compiling Galera Cluster for MariaDB.


--------------------------------------------
Building the Galera Cluster for Maria DB
--------------------------------------------
.. _`Build Galera MariaDB`:

There are two components to Galera Cluster for MariaDB.  The Galera Replication plugin, the  MariaDB server with the write-set replication patch.

To build the Galera Cluster, complete the following steps:

1. Download the Galera Replicator plugin source package from `Launchpad <https://launchpad.net/galera/+download>`_:

   .. code-block:: console

	$ wget https://launchpad.net/galera/2.x/version_nbr/+download/galera-version_nbr-src.tar.gz

   For Github, use:
   
   .. code-block:: console
  
	$ git clone https://github.com/codership/galera.git
	
   Then, extract the package:
   
   .. code-block:: console
	
	$ tar zxf galera-version_nbr-src.tar.gz


2. In the ``galera/`` directory, run  ``scons`` to build the plugin:

   .. code-block:: console

	$ cd galera-version_nbr-src/
	$ scons

3. Download and extract the MariaDB source code with the write-set replication patch from `MariaDB <http://download.mariadb.org/mariadb-galera/>`_:

   .. code-block:: console

	$ wget http://download.mariadb.org/mariadb-galera/version_nbr/mariadb-galera-version_nbr-linux-arch.tar.gz
	$ tar zxf mariadb-galera-version_nbr-linux-arch.tar.gz

4. Build the MariaDB server:

   .. code-block:: console

	$ cmake -DWITH_WSREP=1 \
		-DWITH_INNODB_DISALLOW_WRITES=1
	$ make

Galera Cluster for MariaDB is now installed on your server.

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Updating System Tables
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`Update System Tables`:

If you chose to overwrite an existing installation of MariaDB, you must also upgrade the system tables to the new system.

To upgrade the system tables, after you start the MariaDB server run the following from the command-line:

.. code-block:: console

	$ mariadb_upgrade

If this command generates any errors, check the MariaDB Documentation for more information related to the error messages.  The errors it generates are typically not critical and you can usually ignore them, unless they involve specific functionality that your system requires.


