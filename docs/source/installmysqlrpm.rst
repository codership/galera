=============================================
RPM Installation
=============================================
.. _`mysql-rpm-install`:

If you run Red Hat or an RPM-based distribution, such as CentOS or Fedora, you can install Galera Cluster for MySQL on your server through package downloads.

.. note:: This tutorial omits MySQL authentication options for the sake of brevity.

---------------------------------------------
Preparing Your Server
---------------------------------------------
.. _`prep-server`:

Before you begin installing Galera Cluster, you must first remove any existing installation of MySQL server and install the packages dependencies.

- ``psmisc`` The PSmisc utilities.
- ``mysql`` The MySQL client package.
- ``MySQL-shared-compat`` The MySQL shared compatibility libraries.

To remove an existing server, run the following command as root:

.. code-block:: console

	$ rpm --nodeps --allmatches -e \
		mysql-server \
		mysql-test \
		mysql-bench

To install the package dependencies, complete the following steps:

1. Install ``psmisc`` and the MySQL client:

   .. code-block:: console

	$ yum install psmisc \
		mysql

2. Go to `MySQL <http://dev.mysql.com/downloads/mysql>`_ and download the MySQL Shared Compatibility Libraries.

3. Install the compatibility libraries:

   .. code-block:: console

	$ rpm -i MySQL-shared-compat-*.rpm


---------------------------------------------
Installing Galera Cluster for MySQL
---------------------------------------------
.. _`install-galera`:

There are two packages involved in the installation of Galera Cluster for MySQL:

- ``mysql-server-wsrep`` A new installation of the MySQL server, which includes a patch for write-set replication.

- ``galera`` The Galera Replicator plugin.

To install Galera Cluster for MySQL, complete the following steps:

1. Go to `MySQL Server <https://launchpad.net/codership-mysql/+download>`_ and download the server package.

2. Install the MySQL server package:

   .. code-block:: console

	$ rpm -i mysql-server-wsrep-*.rpm 

3. Go to `Galera Replicator <https://launchpad.net/g alera>`_ and download the Galera plugin.

4. Install the Galera Replicator package:

   .. code-block:: console

	$ rpm -i galera-*.rpm

5. Using a text editor, add to your configuration file, (``my.cnf`` or ``my.ini``, depending on your build), the path to the Galera Replicator plugin.

   .. code-block:: ini
   
	wsrep_provider = /usr/lib/galera/libgalera_smm.so

Galera Cluster is installed on your system, you can now start MySQL.


^^^^^^^^^^^^^^^^^^^^^^^
Upgrading System Tables
^^^^^^^^^^^^^^^^^^^^^^^
.. _`upgrade-sys-tables`:

If you installed Galera Cluster over an existing installation of MySQL, you must also upgrade the system tables to the new system.

To upgrade the system tables, after you start the MySQL server run the following from the command-line:

.. code-block:: console

	$ mysql_upgrade

If this command generates any errors, check the MySQL Documentation for more information related to the error messages.  The errors it generates are typically not critical and you can usually ignore them, unless they involve specific functionality that your system requires.
