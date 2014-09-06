=====================================
Debian Installation
=====================================
.. _`XtraDB Debian Installation`


If you run Debian or a Debian-based distribution, such as Ubuntu, you can install Galera Cluster for Percona XtraDB on your server through the package manager.

---------------------------------------------
Enabling the Percona Repository
---------------------------------------------
.. _`Enable Percona Repo`:

Percona provides a repository for the latest releases of Galera Cluster and Percona XtraDB itself.  This requires that you have Software Properties installed on your server.

On Debian, run the following command as root:

.. code-block:: console

	$ apt-get install python-software-properties

On Ubuntu, instead run this command:

.. code-block:: console

	$ sudo apt-get install software-properties-common

After ``apt-get`` finishes running, you can begin installing the Percona repository.

1. Add the key for the Percona repository to your key server:

   .. code-block:: console

	$ apt-key adv --recv-keys --keyserver \
		keyserver.ubuntu.com 0xcbcb082a1bb943db

2. Add the repository to ``/etc/apt/sources.list``:

   .. code-block:: console

	$ add-apt-repository \
		'deb http://repo.percona.com/apt release_nm main'
	$ add-apt-repository \
		'deb-src http://repo.percona.com/apt release_nm main'

  - For ``release_nm`` use the distribution release name, such as ``wheezy``.  If you don't know the release name for your server, use the following command:
  
    .. code-block:: console

  	$ lsb_release -a

3. Update the local cache:

   .. code-block:: console

	$ apt-get update

Packages in the Percona repository are now available to install on your server.

----------------------------------------------
Installing Galera Cluster for Percona XtraDB
----------------------------------------------
.. _`Install Galera XtraDB`:

The Galera Cluster and Percona XtraDB server configured for write-set replication are included together in one package.

.. note:: If you have an existing installation of the Percona XtraDB server, ``apt-get`` will remove it to install the new patched server.  Additionally, after the installation is complete, you should update your system tables.

To install Galera Cluster run the following command as root:

.. code-block:: console

	$ apt-get install percona-xtradb-cluster

Galera Cluster for Percona XtraDB is now installed on your server.


^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Updating Tables
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. `Update System Tables`:

If you installed *Galera Cluster* over an existing installation of Percona XtraDB, you should update the system tables for the new database server.

To update the system tables, run the following command:

.. code-block:: console

	$ mysql_upgrade
	
If this command generates any errors, check with Percona XtraDB Documentation for their meaning.  The errors it generates are typically uncritical and usually you can ignore them unless they involve specific functionality that your system requires.


