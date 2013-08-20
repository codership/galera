================================================
 Compiling Galera Cluster for MySQL from Source
================================================
.. _`Compiling Galera Cluster for MySQL from Source`:

.. index::
   pair: Installation; Compiling Galera

These instructions describe how to compile
*Galera Cluster* from source.


Build dependencies for the Galera Replication Plugin are:
* System headers
* Bash
* GNU toolchain, gcc/g++ >= 4.4
* Boost libraries >= 1.41
* Check (http://check.sourceforge.net/)
* Scons (http://www.scons.org/)

To build, proceed as follows:

1. Download and extract the Galera Replication Plugin
   source package from
   https://launchpad.net/galera/+download::
  
    $ wget https://launchpad.net/galera/2.x/23.2.6/+download/galera-23.2.6-src.tar.gz
    $ tar zxf galera-23.2.6-src.tar.gz
    $ cd galera-23.2.6-src/

2. Run *scons* to build the Galera Replication Plugin::

    $ scons

3. When the build process is completed, the Galera provider
   library *libgalera_smm.so* can be found in the build
   directory root.

Build the MySQL server as follows:

1. Download MySQL source code from http://dev.mysql.com/downloads/mysql/.
2. Extract the source package::

    $ tar zxf mysql-5.5.31.tar.gz

3. Download and uncompress the *wsrep* patch that
   corresponds to the MySQL version::

    $ wget https://launchpad.net/codership-mysql/5.5/5.5.31-23.7.5/+download/mysql-5.5.31-23.7.5.patch.gz
    $ gunzip mysql-5.5.31-23.7.5.patch.gz

3. Apply the patch::

    $ cd mysql-5.5.31
    $ patch -p0 < ../mysql-5.5.31-23.7.5.patch

4. Build the MySQL server::

    $ cmake -DWITH_WSREP=1 -DWITH_INNODB_DISALLOW_WRITES=1
    $ make 
