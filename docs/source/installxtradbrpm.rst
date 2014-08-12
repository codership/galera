=============================================
RPM Installation
=============================================
.. _`XtraDB RPM Installation

If you run Red Hat Enterprise Linux or another RPM-based distribution, such as Fedora or CentOS, you can install Galera Cluster for Percona XtraDB on your server through the package manager.


---------------------------------------------
Enabling the Percona Repository
---------------------------------------------
.. _`Enable Percona Repo`:

Percona provides a repository for the latest releases of Galera Cluster and Percona XtraDB itself.

To set up the Percona repository, complete the following steps:

1. Using your preferred text editor, create a GPG file for the Percona repository.  For example:

   .. code-block:: console

	$ vim /etc/pki/rpm-gpg/RPB-GPG-KEY-percona

2. Copy the `Percona GPG key <https://www.percona.com/downloads/RPM-GPG-KEY-percona>`_ into the file.

3. Save and exit.

4. Using your preferred text editor, create a ``.repo`` file in the ``/etc/yum.repos.d/`` directory.  For example:

   .. code-block:: console

	$ vim /etc/yum.repos.d/Percona.repos

5. Copy the following information into the file:

   .. code-block:: ini

	# Percona Repository List
	[percona]
	name = Percona XtraDB
	baseurl = htt://repo.percona.com/distro_nm/$releasever/os/$basearch/
	enabled = 1
	gpgkey = file:///etc/pki/rpm-gpg/RPM-GPG-KEY-percona
	gpgcheck = 1

  - For ``distro_nm`` use the name of your Linux distribution, such as ``centos``.

6. Save and exit.

Packages in the MariaDB repository are now available for installation on your server through ``yum``.


------------------------------------------------
Installing Galera Cluster for Percona XtraDB
------------------------------------------------
.. _`Install Galera XtraDb`:

The Galera Cluster and Percona XtraDB server configured for write-set replication are included together in one package.

.. note:: If you have an existing installation of the Percona XtraDB server, ``yum`` will remove it to install the new patched server.  Additionally, after the installation is complete, you should update your system tables.

To install Galera Cluster run the following command as root:

.. code-block:: console

	$ yum install Percona-XtraDB-Cluster

Galera Cluster for Percona XtraDB is now installed on your server.

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Updating Tables
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`Update System Tables`:

If you installed *Galera Cluster* over an existing installation of Percona XtraDB, you should update the system tables for the new database server.

To update the system tables, run the following command:

.. code-block:: console

	$ mysql_upgrade
	
If this command generates any errors, check with Percona XtraDB Documentation for their meaning.  The errors it generates are typically uncritical and usually you can ignore them unless they involve specific functionality that your system requires.


