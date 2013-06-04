======================================================
 Installing Galera Cluster for MySQL
======================================================
.. _`Downloading and Installing Galera Cluster for MySQL`:
.. index::
   pair: Installation; Galera Cluster for MySQL

If you want to install *Galera Cluster for MySQL*,
proceed as follows:

1. Download the *Galera Cluster for MySQL* as a binary package for your
   Linux distribution from (https://launchpad.net/codership-mysql/+download).
2. Verify the download using the MD5 sum that Launchpad generates.
3. Follow the Linux distribution specific instructions in the
   chapters below.

.. note:: In the examples below, MySQL authentication options
          are omitted for brevity.

-------------------------------------------------------------------------------
Installing Galera Cluster for MySQL on Debian and Debian-derived Distributions
-------------------------------------------------------------------------------

This chapter describes how to install *Galera Cluster for MySQL* on Debian
and Debian-derived distributions.

Before You Start
================

Upgrade from *mysql-server-5.0* to *mysql-wsrep* is not supported.
Upgrade to *mysql-server-5.1*.

If you are installing over an existing MySQL installation,
*mysql-server-wsrep* will conflict with the
*mysql-server-5.1* package. Remove the package as follows::

    $ sudo apt-get remove mysql-server-5.1 mysql-server-core-5.1

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

Installation
=============
Install the *mysql-wsrep* package as follows::

    $ sudo dpkg -i <mysql-server-wsrep DEB>

----------------------------------------------------------------------------------
Installing Galera Cluster for MySQL on CentOS and Similar RPM-based Distributions
----------------------------------------------------------------------------------

This chapter describes how to install *Galera Cluster for MySQL* on CentOS and
similar RPM-based distributions.

Before You Start
================

If you are migrating from an existing MySQL installation, there are two optins:

- If you're already using official MySQL-server-community 5.1.x RPM from
  Oracle::

     # rpm -e mysql-server

- If you are upgrading from the stock *mysql-5.0.77* on CentOS:

     1. Make sure that the following packages are not installed::
     
	      # rpm --nodeps --allmatches -e mysql-server mysql-test mysql-bench

     2. Install the official *MySQL-shared-compat-5.1.x* from
        http://dev.mysql.com/downloads/mysql/5.1.html


Installation
=============

Install the *mysql-wsrep* package as follows::

   # rpm -Uvh <MySQL-server-wsrep RPM>

.. note:: If the installation fails due to missing dependencies,
          install the missing packages (for example, *yum install perl-DBI*)
          and retry.

-------------------------------
Installing Additional Packages
-------------------------------

Install also the following additional packages (if not yet installed):

- *galera*, a multi-master replication provider, https://launchpad.net/galera.
- *MySQL-client-community* for connecting to the server and for the
  *mysqldump*-based SST.
- *rsync* for the *rsync*-based SST
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