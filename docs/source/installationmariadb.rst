=========================================================
 Downloading and Installing MariaDB Galera Cluster
=========================================================
.. _`Downloading and Installing MariaDB Galera Cluster`:
.. index::
   pair: Installation; MariaDB Galera Cluster

If you have decided to install Galera on a MariaDB Cluster,
proceed as follows:

1. Download the MariaDB Galera Cluster
   (http://downloads.mariadb.org/mariadb-galera).
2. Download the Galera Replication Plugin
   (https://launchpad.net/galera/+download).
3. Extract the downloaded packages.
4. Install the downloaded packages.

-----------------------------
 Installing by Using apt-get
-----------------------------

If you prefer to install by using *apt-get*, ensure that you have
defined the correct source repository for you operating system.
The Galera cluster software package is included in the MariaDB 
repositories to make installation easier.

To install, open a terminal window and issue the command below:

::

    $ sudo apt-get install mariadb-galera-server galera

.. note:: If the server already has the *mariadb-server* package
          installed, it will be automatically removed prior to
          installing *mariadb-galera-server*.

-------------------------
 Installing by Using yum
-------------------------

If you prefer to install by using *yum*, ensure that you have
defined the correct source repository for you operating system.
The Galera cluster software package is included in the MariaDB 
repositories to make installation easier.

To install, open a terminal window and issue the command below:

::

    $ sudo yum install MariaDB-Galera-server MariaDB-client galera

.. note:: If you do not have the *MariaDB GPG Signing key*
          installed, YUM will prompt you to install it after
          downloading the packages (but before installing them).

.. note:: If the server already has the *MariaDB-server*
          package installed, you might need to remove it prior
          to installing *MariaDB-Galera-server* (with the
          ``sudo yum remove MariaDB-server`` command). No
          databases are removed when the *MariaDB-server*
          package is removed, but as with any upgrade, it
          is best to have backups.
