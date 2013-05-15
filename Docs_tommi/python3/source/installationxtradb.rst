=========================================================
 Downloading and Installing Percona XtraDB Cluster
=========================================================
.. _`Downloading and Installing Percona XtraDB Cluster`:
.. index::
   pair: Installation; Percona XtraDB Cluster

If you have decided to install Galera on a Percona XtraDB Cluster,
proceed as follows:

1. Download the Percona XtraDB Cluster
   (http://www.percona.com/downloads/Percona-XtraDB-Cluster/).
2. Extract the downloaded package.
3. Install the downloaded package.

------------------------------
 Installing by Using apt-get
------------------------------

If you prefer to install by using *apt-get*, ensure that you have
defined the correct source repository for you operating system.
The Galera cluster software package is included in the XtraDB 
repositories to make installation easier.

To install, open a terminal window and issue the command below:

::

    $ sudo apt-get install percona-xtradb-cluster-server-5.5 percona-xtradb-cluster-client-5.5

.. note:: Replace the package name with the version you want to install.

----------------------------
 Installing by Using yum
----------------------------

If you prefer to install by using *yum*, ensure that you have
defined the correct source repository for you operating system.
The Galera cluster software package is included in the XtraDB 
repositories to make installation easier.

To install, open a terminal window and issue the command below:

::

    $ sudo yum install Percona-XtraDB-Cluster-server Percona-XtraDB-Cluster-client

.. note:: Replace the package name with the version you want to install.
