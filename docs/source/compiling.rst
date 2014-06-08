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

1. Download and extract the Galera Replication Plugin::
  
    $ wget https://github.com/codership/galera/tarball/3.x -O galera.tar.gz
    $ tar -zxf galera.tar.gz
    $ cd galera

   or::
 
    $ git clone https://github.com/codership/galera.git
    $ cd galera

2. Run *scons* to build the Galera Replication Plugin::

    $ scons

3. When the build process is completed, the Galera provider
   library *libgalera_smm.so* can be found in the build
   directory root.

Build the MySQL server as follows:

1. Select a wsrep patch for mysql from https://launchpad.net/codership-mysql/+download

   It will have a filename like mysql-5.6.16_wsrep_25.5.patch

2. Download MySQL source code from http://downloads.mysql.com/archives/community/

   In this page change `Select Platform` to `Source Code`.

   Download the version corresponding to the patch e.g. 5.6.16.

3. Extract the source package::

    $ tar zxf mysql-5.6.16.tar.gz

4. Apply the patch::

    $ cd mysql-5.6.16
    $ patch -p1 < ../mysql-5.6.16_wsrep_25.5.patch

   Skip the .bzrignore file as it doesn't exist the tarball by pressing enter twice::

    $ patch -p1 < ../mysql-5.6.16_wsrep_25.5.patch
    can't find file to patch at input line 4
    Perhaps you used the wrong -p or --strip option?
    The text leading up to this was:
    --------------------------
    |=== modified file '.bzrignore'
    |--- old/.bzrignore     2013-02-05 21:49:04 +0000
    |+++ new/.bzrignore     2013-09-01 09:27:10 +0000
    --------------------------
    File to patch: 
    Skip this patch? [y] 
    Skipping patch.
    3 out of 3 hunks ignored
    patching file CMakeLists.txt
    patching file Docs/README-wsrep
    patching file cmake/configure.pl
    ...


5. Build the MySQL server::
 

    $ cmake -DWITH_WSREP=1 -DWITH_INNODB_DISALLOW_WRITES=1
    $ make 

   See the following URL if you have missing dependancies http://dev.mysql.com/doc/refman/5.6/en/source-installation.html.
