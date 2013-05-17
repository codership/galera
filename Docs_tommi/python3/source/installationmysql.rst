======================================================
 Downloading and Installing Galera Cluster on MySQL
======================================================
.. _`Downloading and Installing Galera Cluster on MySQL`:
.. index::
   pair: Installation; Galera Cluster on MySQL

If you have decided to install Galera on a MySQL Cluster,
proceed as follows:

1. Download the Galera Cluster as a binary package for your
   Linux distribution from (https://launchpad.net/codership-mysql/+download).
2. Verify the download using the MD5 sum that Launchpad generates.
3. Follow the Linux distribution specific instructions in the
   chapters below.

.. note:: In the examples below, MySQL authentication options
          are omitted for brevity.

---------------------------------------------------------------------
Installing Galera Cluster on Debian and Debian-derived Distributions
---------------------------------------------------------------------

This chapter describes how to install Galera Cluster on Debian
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

------------------------------------------------------------------------
Installing Galera Cluster on CentOS and Similar RPM-based Distributions
------------------------------------------------------------------------

This chapter describes how to install Galera Cluster on CentOS and
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
- *xtrabackup* and *nc* for the *xtrabackup*-based SST.

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

=============================
Configuring the Installation
=============================

Unless you are upgrading an already installed *mysql-wsrep*
package, you must configure the installation to prepare the
server for operation.

--------------------
Configuration Files
--------------------

Edit the *my.cnf* configuration file as follows:

- Make sure that the system-wide *my.cnf* file does not bind *mysqld*
  to 127.0.0.1. To be more specific, if you have the following line
  in the [mysqld] section, comment it out::

      #bind-address = 127.0.0.1

- Make sure that the system-wide *my.cnf* file contains the line below::
  
    !includedir /etc/mysql/conf.d/

Edit the */etc/mysql/conf.d/wsrep.cnf* configuration file as follows:

- Set the *wsrep_provider* option by specifying a path to the galera 
  provider library. If you do not have a provider, leave it as it is.
- When a new node joins the cluster, it will have to receive a state
  snapshot from one of the peers. This requires a privileged MySQL
  account with access from the rest of the cluster. Set the *mysql*
  login/password pair for SST, for example, as follows::

      wsrep_sst_auth=wsrep_sst:wspass

---------------------
Database Privileges
---------------------

Restart the MySQL server and connect to it as root to grant privileges
to the SST account. Furthermore, empty users confuse MySQL authentication
matching rules. Delete them::

    $ mysql -e "SET wsrep_on=OFF; DELETE FROM mysql.user WHERE user='';"
    $ mysql -e "SET wsrep_on=OFF; GRANT ALL ON *.* TO wsrep_sst@'%' IDENTIFIED BY 'wspass'";

------------------
Firewall Settings
------------------

The *MySQL-wsrep* server must be accessible from other cluster members through
its client listening socket and through the wsrep provider socket. See your
distribution and wsrep provider documentation for details. For example, on
CentOS you could use these settings::

    # iptables --insert RH-Firewall-1-INPUT 1 --proto tcp --source <my IP>/24 --destination <my IP>/32 --dport 3306 -j ACCEPT
    # iptables --insert RH-Firewall-1-INPUT 1 --proto tcp --source <my IP>/24 --destination <my IP>/32 --dport 4567 -j ACCEPT

If there is a NAT firewall between the nodes, configure it to allow
direct connections between the nodes (for example, through port forwarding).

--------
SELinux
--------

If you have SELinux enabled, it may block *mysqld* from carrying out the
required operations. Disable SELinux or configure it to allow *mysqld*
to run external programs and open listen sockets at unprivileged ports
(that is, things that an unprivileged user can do). See SELinux
documentation for more information.

To disable SELinux, proceed as follows:

1) run *setenforce 0* as root.
2) set ``SELINUX=permissive`` in  */etc/selinux/config*

---------
AppArmor
---------

AppArmor is always included in Ubuntu. It may prevent *mysqld* from
opening additional ports or run scripts. See AppArmor documentation
for more information on its configuration.

To disable AppArmor, proceed as follows::

    $ cd /etc/apparmor.d/disable/
    $ sudo ln -s /etc/apparmor.d/usr.sbin.mysqld
    $ sudo service apparmor restart
