=========================================
Installing Galera Cluster for MySQL from Source
=========================================
.. _'MySQL Source Installation'

If you run a machine that does not support Debian- or RPM-based binary installations, you can install Galera Cluster for MySQL by compiling from source.


.. note:: This tutorial omits MySQL authentication options for brevity.

-----------------------------------------
Build Dependencies
-----------------------------------------
.. _`Build Dependencies`:

In order to install Galera Cluster for MySQL from source, you must first install the build dependencies on your server.

- System headers
- Bash
- GNU toolchain, gcc/g++ >= 4.4
- Boost libraries >= 1.41
- `Check <http://check.sourceforge.net/>`_
- `Scons <http://www.scons.org/>`_

Once you have these installed, you can begin compiling Galera Cluster.


--------------------------------------------
Building Galera Cluster for MySQL
--------------------------------------------
.. `Build Galera MySQL`:

There are three components to Galera Cluster for MySQL.  The Galera Replication plugin, the  MySQL server and the write-set replication patch for the MySQL server.

To build Galera Cluster, complete the following steps:

1. Download the Galera Replicator plugin source package from `Launchpad <https://launchpad.net/galera/+download>`_::

	$ wget https://launchpad.net/galera/2.x/version_nbr/+download/galera-version_nbr-src.tar.gz
	$ tar zxf galera-version_nbr-src.tar.gz


2. In the `galera/` directory, run  `scons` to build the plugin::

	$ cd galera-version_nbr-src/
	$ scons

3. Download and extract the MySQL source code from `MySQL <http://dev.mysql.com/downloads/mysql/>`_::

	$ wget http://dev.mysql.com/get/downloads/mysql-version_nbr/mysql-version_nbr.tar.gz
	$ tar zxf mysql-version_nbr.tar.gz

4. Download and uncompress the write-set replication patch for your version of MySQL from `Launchpad <https://launchpad.net/codership-mysql>`_::

	$ wget https://launchpad.net/codership-mysql/version_nbr/+download/mysql-version_nbr.patch.gz
	$ gunzip mysql-version_nbr.patch.gz

5. Apply the patch::

	$ cd mysql-version_nbr/
	$ patch -p0 <../mysql-version_nbr.patch

6. Build the MySQL server::

	$ cmake -DWITH_WSREP=1 \
		-DWITH_INNODB_DISALLOW_WRITES=1
	$ make

.. note:: If you are building on a server that has an existing installation of MySQL and do not want to overwrite it, run CMake with different values for ``CMAKE_INSTALL_PREFIX``, ``MYSQL_TCP_PORT`` and ``MYSQL_UNIX_ADDR`` than those used by the existing installation.

Galera Cluster for MySQL is now installed on your server.

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Updating System Tables
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`Update System Tables`:

If you chose to overwrite an existing installation of MySQL, you must also upgrade the system tables to the new system.

To upgrade the system tables, after you start the MySQL server run the following from the command-line::

	$ mysql_upgrade

If this command generates any errors, check the MySQL Documentation for more information related to the error messages.  The errors it generates are typically not critical and you can usually ignore them, unless they involve specific functionality that your system requires.

