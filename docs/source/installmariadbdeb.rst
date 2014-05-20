=============================================
Debian Installation
=============================================
.. _`MariaDB Debian Installation`

If you run Debian or a Debian-based distribution, such as Ubuntu, you can install Galera Cluster for MariaDB on your server through the package manager.


---------------------------------------------
Enabling the MariaDB Repository
---------------------------------------------
.. _`MariaDB Repo`:

MariaDB provides a repository for the latest releases of Galera Cluster and MariaDB itself.  This requires that you have Software Properties installed on your server.

On Debian, run the following command as root::

	$ apt-get install python-software-properties

On Ubuntu, instead run this command::

	$ sudo apt-get install software-properties-common

After ``apt-get`` finishes running, you can begin installing the MariaDB repository.

1. Add the GnuPG key for the MariaDB repository to your key server::

	$ apt-key adv --recv-keys --keyserver \
		keyserver.ubuntu.com 0xcbcb082a1bb943db

2. Add the repository to ``/etc/apt/sources.list``::

	$ add-apt-repository \
		'deb http://mirror.jmu.edu/pub/mariadb/repo/version_nbr/distro_nm release_nm man'


  - For ``version_nbr`` use the MariaDB version number you want to use.

  - For ``distro_nm`` use the name of your Linux distribution, such as ``ubuntu``.

  - For ``release_nm`` use the distribution release name, such as ``wheezy``.  If you don't know the release name for your server, use the following command::

  	$ lsb_release -a

3. Update the local cache::

	$ apt-get update

Packages in the MariaDB repository are now available to install on your server.

For more information on the repository or alternative mirrors, please see the `MariaDB Repository Configuration Tool <https://downloads.mariadb.org/mariadb/repositories/>`_.


---------------------------------------------
Installing Galera Cluster for MariaDB
---------------------------------------------
.. _`Install Galera MariaDB`:

There are three packages involved in the installation of Galera Cluster for MariaDB.

- MariaDB Client, a command line tool for accessing the database.

- MariaDB Server configured with the write-set replication patch.

- Galera Replication Plugin.

.. note:: If you have an existing installation of the MariaDB server, ``apt-get`` will remove it to install the new patched server.  Additionally, after the installation is complete, you should update your system tables.

To install Galera Cluster run the following command as root::

	$ apt-get install mariadb-client \
		mariadb-galera-server \
		galera

Galera Cluster for MariaDB is now installed on your server.

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Updating System Tables
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`Update System Tables`:

If you installed Galera Cluster over an existing installation of MariaDB, you should update the system tables for the new server installation.

To update the system tables, run the following command::

	$ mariadb_upgrade

If this command generates any errors, check with the MariaDB Documentation for their meaning.  The errors it generates are typically uncritical and usually you can ignore them unless they involve specific functionality that you require.