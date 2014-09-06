=========================================
Source Installation
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
- GNU toolchain, gcc/g++, version 4.4 or later
- Boost libraries, version 1.41 or later
- `Check <http://check.sourceforge.net/>`_
- `Scons <http://www.scons.org/>`_

Once you have these installed, you can begin compiling Galera Cluster.

--------------------------------------------
Building Galera Cluster for MySQL
--------------------------------------------
.. `Build Galera MySQL`:

There are three components in compiling Galera Cluster for MySQL.  The Galera Replication plugin, the  MySQL server and the write-set replication patch for the MySQL server.

To build Galera Cluster, complete the following steps:

1.  Download the Galera Replicator plugin source package from `Launchpad <https://launchpad.net/galera/+download>`_ or through Github.  For Launchpad, use:

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

3. Download and extract the MySQL source code from `MySQL <http://dev.mysql.com/downloads/mysql/>`_:

   .. code-block:: console

	$ wget http://dev.mysql.com/get/downloads/mysql-version_nbr/mysql-version_nbr.tar.gz
	$ tar zxf mysql-version_nbr.tar.gz

4. Download and uncompress the write-set replication patch for your version of MySQL from `Launchpad <https://launchpad.net/codership-mysql>`_:

   .. code-block:: console

	$ wget https://launchpad.net/codership-mysql/version_nbr/+download/mysql-version_nbr.patch.gz
	$ gunzip mysql-version_nbr.patch.gz

5. Apply the patch:

   .. code-block:: console
   
	$ cd mysql-version_nbr/
	$ patch -p0 <../mysql-version_nbr.patch

   Press enter twice to skip ``.bzrignore``.  It does not exist in the tarball.  You should get results that look something like this:
   
   .. code-block:: console
	
	$ patch -p1 < ../mysql-5.6.16_wsrep_25.5.patch
	  can't find file to patch at input line 4
	  Perhaps you used the wrong -p or --strip option?
	  The text leading up to this was:
	 --------------------------
	 |=== modified file '.bzrignore'
	 |--- old/.bzrignore     2013-02-05 21:49:04 +0000
	 |+++ new/.bzrignore     2013-09-01 09:27:10 +0000
	 --------------------------
	 File to patch:
	 Skip this patch? [y]
	 Skipping patch.
	 3 out of 3 hunks ignored
	 patching file CMakeLists.txt
	 patching file Docs/README-wsrep
	 patching file cmake/configure.pl
	...

6. Build the MySQL server:

   .. code-block:: console

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

To upgrade the system tables, after you start the MySQL server run the following from the command-line:

.. code-block:: console

	$ mysql_upgrade

If this command generates any errors, check the MySQL Documentation for more information related to the error messages.  The errors it generates are typically not critical and you can usually ignore them, unless they involve specific functionality that your system requires.

