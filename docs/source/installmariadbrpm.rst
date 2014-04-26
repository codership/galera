=============================================
Installing Galera Cluster for MariaDB on RPM-based Distributions
=============================================
.. _`MariaDB RPM Installation`

If you run Red Hat Enterprise Linux or another RPM-based distribution, such as Fedora or CentOS, you can install Galera Cluster for MariaDB on your server through the package manager.

---------------------------------------------
Enabling the MariaDB Repository
---------------------------------------------
.. _`Enable MariaDB Repo`:

MariaDB provides a repository for the latest releases of Galera Cluster and MariaDB itself.

To set up the MariaDB repository, complete the following steps:

1. Using your preferred text editor, create a ``.repo`` file in the ``/etc/yum.repos.d/`` directory.  For example::

	$ vim /etc/yum.repos.d/MariaDB.repos

2. Copy the following information into the file::

	# MariaDB Repository List
	[mariadb]
	name = MariaDB
	baseurl = htt://yum.mariadb.org/version_nbr/package_nm
	gpgkey = https://yum.mariadb.org/RPM-GPG-KEY-MariaDB
	gpgcheck=1

  - For ``version_nbr`` use the version of MariaDB that you want to use.

  - For  ``package_nm`` use the package name for your server.  For example, ``rhel6-amd64`` for a Red Hat Enterprise Linux 6 server on amd64 architecture.

3. Save the file.

Packages in the MariaDB repository are now available for installation on your server through ``yum``.

For more information on the repository, package names or available mirrors, please see the `MariaDB Repository Configuration Tool <https://downloads.mariadb.org/mariadb/repositories/>`_.

------------------------------------------------
Installing Galera Cluster for MariaDB
------------------------------------------------
.. _`Install Galera MariaDB`:

There are three packages involved in the installation of Galera Cluster for MariaDB.

- MariaDB Client, a command line tool for accessing the database.

- MariaDB Server configured with the write-set replication patch.

- Galera Replication Plugin.

.. note:: If you have an existing installation of the MariaDB server, ``yum`` will remove it to install the new patched server.  Additionally, after the installation is complete, you should update your system tables.

To install Galera Cluster run the following command as root::

	$ yum install MariaDB-client \
		MariaDB-Galera-server \
		galera

Galera Cluster for MariaDB is now installed on your server.


^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Updating Tables
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`Update System Tables`:

If you installed *Galera Cluster* over an existing installation of MariaDB, you should update the system tables for the new database server.

To update the system tables, run the following command::

	$ mariadb_upgrade
	
If this command generates any errors, check with MariaDB Documentation for their meaning.  The errors it generates are typically uncritical and usually you can ignore them unless they involve specific functionality that your system requires.

