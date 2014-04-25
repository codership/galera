=============================================
Installing Galera Cluster for MySQL on RPM-based Distributions
=============================================
.. _`MySQL RPM Installation`

If you run Red Hat or an RPM-based distribution, such as CentOS or Fedora, you can install Galera Cluster for MySQL on your server through package downloads.

.. note:: This tutorial omits MySQL authentication options for the sake of brevity.

---------------------------------------------
Preparing Your Server
---------------------------------------------

Before you begin installing Galera Cluster, you must first remove any existing installation of MySQL server and install the packages dependencies.

To remove an existing server, run the following command as root::

	$ rpm --nodeps --allmatches -e \
		mysql-server \
		mysql-test \
		mysql-bench

To install the package dependencies, complete the following steps:

1. Install ``psmisc`` and the MySQL client::

	$ yum install psmisc \
		mysql-client

2. Go to `MySQL <http://dev.mysql.com/downloads/mysql>`_ and download the MySQL Shared Compatibility Libraries.

3. Install the compatibility libraries::

	$ rpm -e mysql-shared-compat.rpm


---------------------------------------------
Installing Galera Cluster for MySQL
---------------------------------------------

There are two packages involved in the installation of Galera Cluster for MySQL:

- ``mysql-server-wsrep``: A new installation of the MySQL server, which includes a patch for write-set replication.

- ``galera``: The Galera Replicator plugin.

To install Galera Cluster for MySQL, complete the following steps:

1. Go to `MySQL Server <https://launchpad.net/codership-mysql/+download>`_ and download the server package.

2. Install the MySQL server package::

	$ rpm -e mysql-server-wsrep.rpm 

3. Go to `Galera Replicator <https://launchpad.net/g alera>`_ and download the Galera plugin.

4. Install the Galera Replicator package::

	$ rpm -e galera.rpm

5. Configure the MySQL server to use the Galera Replicator plugin in ``my.cnf``.

	``wsrep_provider=/usr/lib/galera/libgalera_smm.so``

Galera Cluster is installed on your system, you can now start MySQL.


^^^^^^^^^^^^^^^^^^^^^^^
Upgrading System Tables
^^^^^^^^^^^^^^^^^^^^^^^

If you installed Galera Cluster over an existing installation of MySQL, you must also upgrade the system tables to the new system.

To upgrade the system tables, after you start the MySQL server run the following from the command-line::

	$ mysql_upgrade

If this command generates any errors, check the MySQL Documentation for more information related to the error messages.  The errors it generates are typically not critical and you can usually ignore them, unless they involve specific functionality that your system requires.
