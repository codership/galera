Codership Oy
http://www.codership.com
<info@codership.com>

This is Galera replication - Codership's implementation of the write set
replication (wsrep) interface (https://github.com/codership/wsrep-API).
The software and other files in this directory unless otherwise noted
are distributed under GPLv2, see COPYING for details.


BUILDING - General

Build Requirements:
* CMake build system https://cmake.org/
* Check unit test library http://check.sourceforge.net/
* Boost devel package http://www.boost.org/
* OpenSSL devel package

Optional:
* Asio C++ library devel package (http://think-async.com/)

To build with CMake, do:
 $ cmake .
 $ make
Run unit tests with CMake:
 $ make test

Note that Scons has been obsoleted since Galera 3.32, but it is still possible
to compile by running:
 $ scons
(default optimized build)
or
 $ ./scripts/build.sh --scons
(see ./scripts/build.sh --help for information on options)


To build MySQL/Galera demo distribution, clone
https://github.com/codership/mysql-wsrep into some directory (hereafter
<MYSQL_DIR>) and run build script from this directory (hereafter <GALERA_DIR>):

 $ cd <MYSQL_DIR>
 $ git clone --single-branch --branch 5.6 \
       https://github.com/codership/mysql-wsrep.git
 $ cd <GALERA_DIR>
 $ MYSQL_SRC=<MYSQL_DIR>/mysql-wsrep ./scripts/mysql/build.sh -b -s -o -t

After successful build, demo package can be found under scripts/mysql.

BUILDING - on Ubuntu 16.04 or later

 1) apt-get install check cmake libasio-dev libboost-dev \
         libboost-program-options-dev libssl-dev

Then proceed as described above.

BUILDING - on CentOS/RHEL 6 or later

 1) yum install gcc gcc-c++ cmake boost-devel check-devel

Then proceed as described above.

For more information, see:
* Codership mailing list: http://groups.google.com/group/codership-team
* http://galeracluster.com/
* Codership home page: http://www.codership.com
