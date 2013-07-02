======================================================
 Installing Galera Cluster for MySQL
======================================================
.. _`Installing Galera Cluster for MySQL`:
.. index::
   pair: Installation; Galera Cluster for MySQL

If you want to install *Galera Cluster*,
proceed as follows:

1. Download the write set replication patches for MySQL as a binary package for your
   Linux distribution from (https://launchpad.net/codership-mysql/+download).
2. Download Galera replication Plugin, a generic synchronous multi-master
   replication plugin for transactional applications from
   (https://launchpad.net/galera).
3. Verify the downloads using the MD5 sums that Launchpad generates.
4. Follow the Linux distribution specific instructions in the
   chapters below.

.. note:: In the examples below, MySQL authentication options
          are omitted for brevity.

.. note:: If you want to create a more sophisticated setup right at the
          beginning, see chapter :ref:`Configuring Galera Cluster for MySQL <Configuring Galera Cluster for MySQL>`.

---------------------------------------------------------------
Installing Galera Cluster for MySQL on DEB-based Distributions
---------------------------------------------------------------

This chapter describes how to install *Galera Cluster* on Debian
and Debian-derived distributions. 

Upgrade from *mysql-server-5.0* to *mysql-wsrep* is not supported.
Upgrade to *mysql-server-5.5*.

If you are installing over an existing MySQL installation,
*mysql-server-wsrep* will conflict with the
*mysql-server-5.5* package. Remove the package as follows::

    $ sudo apt-get remove mysql-server-5.5 mysql-server-core-5.5

The *mysql-server-wsrep* package requires packages *psmisc* and
*mysql-client-5.1.47* (or later). The MySQL 5.1 packages can be
found in backports repositories. For more information on configuring
and using Debian or Ubuntu backports, see:

- http://backports.debian.org
- https://help.ubuntu.com/community/UbuntuBackports

For example, the installation of the required packages on Debian
Lenny proceeds as follows::

    $ sudo apt-get install psmisc
    $ sudo apt-get -t lenny-backports install mysql-client-5.1

To install *Galera Cluster*, proceed as follows:

1. Install the write set replication patches:

   ``$ sudo dpkg -i <mysql-server-wsrep DEB>``

2. Configure the write set replication patches to use the
   Galera Replication Plugin as a *wsrep provider*:
   
   ``wsrep_provider=/usr/lib/galera/libgalera_smm.so``

3. Start the MySQL server.


---------------------------------------------------------------
Installing Galera Cluster for MySQL on RPM-based Distributions
---------------------------------------------------------------

This chapter describes how to install *Galera Cluster* on CentOS and
similar RPM-based distributions.

If you are migrating from an existing MySQL installation, there are two options:

- If you're already using official MySQL-server-community 5.5.x RPM from
  Oracle:

     ``# rpm -e mysql-server``

- If you are upgrading from the stock *mysql-5.0.77* on CentOS:

     1. Make sure that the following packages are not installed:
     
        ``# rpm --nodeps --allmatches -e mysql-server mysql-test mysql-bench``

     2. Install the official *MySQL-shared-compat-5.5.x* from
        http://dev.mysql.com/downloads/mysql/5.1.html

To install *Galera Cluster*, proceed as follows:

1. Install the write set replication patches:

   ``# rpm -Uvh <MySQL-server-wsrep RPM>``

2. Configure the write set replication patches to use the
   Galera Replication Plugin as a *wsrep provider*:
   
   ``wsrep_provider=/usr/lib64/galera/libgalera_smm.so``

3. Start the MySQL server.

.. note:: If the installation fails due to missing dependencies,
          install the missing packages (for example, *yum install perl-DBI*)
          and retry.

-------------------------------
Installing Additional Packages
-------------------------------

Install also the following additional packages (if not yet installed):

- *MySQL-client-community* for connecting to the server and for the
  *mysqldump*-based SST (for RPM-based distributions only).
- *rsync* for the *rsync*-based SST.
- *xtrabackup* and *nc* (*netcat*) for the *xtrabackup*-based SST.

-----------------------
Upgrading System Tables
-----------------------

If you're upgrading a previous MySQL installation, upgrade the
system tables as follows:

1. Start *mysqld*.
2. Run the *mysql_upgrade* command.

See the MySQL documentation in case of errors. The errors are
usually uncritical and can be ignored unless specific functionality
is needed.
