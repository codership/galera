================================================
 Compiling Galera Cluster for MySQL from Source
================================================
.. _`Compiling Galera Cluster for MySQL from Source`:

.. index::
   pair: Installation; Compiling Galera

These instructions describe how to compile *Galera
Cluster for MySQL* from source.

The latest revision of *Galera Cluster for MySQL* can be
compiled on Solaris 11 x86 and in Linux. You cannot compile
*Galera Cluster for MySQL* on Solaris 10 or on computers
using the big endian byte order.

Proceed as follows:

1. Download the *Galera Cluster for MySQL* source code from
   the latest Galera tree from Launchpad:
   
   https://code.launchpad.net/~codership/galera/2.x
2. Before compiling, ensure that (in this order):

    - you have installed system headers
    - you have installed *bash*
    - you have a full GNU toolchain installed: *GNU make*, *GNU ld*, and so on
    - you have *GCC* >= 4.4
    - you have **/usr/gnu/bin** as the first element in your **PATH**
    - you installed boost libraries >= 1.41 (http://www.boost.org/users/download/)
    - you have installed *check* (http://check.sourceforge.net/)
    - you have installed *scons* (http://www.scons.org/)

   .. note:: If you install to **/usr/local**, ensure that **/usr/local/lib**
             is in your linker path:

             ``$ sudo crle -u -l /usr/local/lib``

3. Run *./scripts/build.sh*.
