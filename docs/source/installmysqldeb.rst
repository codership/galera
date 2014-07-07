=============================================
Debian Installation
=============================================
.. _`MySQL Debian Installation`

If you run Debian or a Debian-based distribution, such as Ubuntu, you can install Galera Cluster for MySQL on your server through package downloads.

.. note:: This tutorial omits MySQL authentication options for the sake of brevity.

---------------------------------------------
Preparing Your Server
---------------------------------------------
.. _`Preparing Your Server`:

Before you begin installing Galera Cluster, you must first remove any existing installation of MySQL server and install the packages dependencies.

To remove an existing server, run the following command as root:

.. code-block:: console

	$ apt-get remove mysql-server \
		mysql-server-core

To install the package dependencies, run the following command as root::

1. Install ``psmisc`` and the MySQL client:

   .. code-block:: console

	$ apt-get install psmisc \
		mysql-client

2. Go to `MySQL <http://dev.mysql.com/downloads/mysql>`_ and download the MySQL Shared Compatibility Libraries.

3. Install the MySQL Shared Compatibility Libraries:

   .. code-block:: console

	$ dpkg -i mysql-shared-compat.deb


---------------------------------------------
Installing Galera Cluster for MySQL
---------------------------------------------
.. _`Install Galera MySQL`:

There are two packages involved in the installation of Galera Cluster for MySQL:

- ``mysql-server-wsrep``: A new installation of the MySQL server, which includes a patch for write-set replication.

- ``galera``: The Galera Replicator plugin.

To install Galera Cluster for MySQL, complete the following steps:

1. Go to `MySQL Server <https://launchpad.net/codership-mysql/+download>`_ and download the server package.

2. Install the MySQL server package:

   .. code-block:: console

	$ dpkg -i mysql-server-wsrep.deb 

3. Go to `Galera Replicator <https://launchpad.net/g alera>`_ and download the Galera plugin.

4. Install the Galera Replicator package:

   .. code-block:: console

	$ dpkg -i galera.deb

5. Configure the MySQL server to use the Galera Replicator plugin in ``my.cnf``:

   .. code-block:: ini

	wsrep_provider = /usr/lib/galera/libgalera_smm.so

Galera Cluster is installed on your system, you can now start MySQL.


^^^^^^^^^^^^^^^^^^^^^^^
Upgrading System Tables
^^^^^^^^^^^^^^^^^^^^^^^
.. _`Upgrade System Tables`:

If you installed Galera Cluster over an existing installation of MySQL, you must also upgrade the system tables to the new system.

To upgrade the system tables, after you start the MySQL server run the following from the command-line:

.. code-block: console

	$ mysql_upgrade

If this command generates any errors, check the MySQL Documentation for more information related to the error messages.  The errors it generates are typically not critical and you can usually ignore them, unless they involve specific functionality that your system requires.
