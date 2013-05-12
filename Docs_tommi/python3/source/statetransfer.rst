.. raw:: html

    <style> .red {color:red} </style>

.. raw:: html

    <style> .green {color:green} </style>

.. role:: red
.. role:: green

==========================
 State Snapshot Transfer
==========================
.. _`State Snapshot Transfer`:

State Snapshot Transfer (SST) refers to a full data copy from
one cluster node (donor) to the joining node (joiner). 
SST is used when a new node joins the cluster. To get synchronized
with the cluster, the new node has to transfer data from a node
that is already part of the cluster. In Galera Replication, you
can choose from two conceptually different ways to transfer a
state from one MySQL server to another:

- *mysqldump* |---| *mysqldump* requires the receiving server to be
  fully initialized and ready to accept connections
  *before* the transfer. 

  *mysqldump* is a blocking method as the donor node
  becomes ``READ-ONLY`` while data is being copied
  from one node to another (SST applies the ``FLUSH
  TABLES WITH READ LOCK`` command for the donor).

  *mysqldump* is also the slowest SST method and, in a
  loaded cluster, this can be an issue.
- *rsync* / *rsync_wan* |---| *rsync* / *rsync_wan*
  copies data files directly. This requires that the
  receiving server is initialized *after* the transfer.
  Methods such as *rsync*, *rsync_wan*, *xtrabackup*
  and other methods fall into this category.

  These methods are faster than *mysqldump*, but they have
  certain limitations. They can only be used on server startup,
  the receiving server must be configured very similarly to
  the donor (for example, the ``innodb_file_per_table``
  value must be the same, and so on). 

  Some of these methods, for example *xtrabackup*, can be
  made non-blocking on donor. These methods are supported
  through a scriptable SST interface.

For more information, see chapter
`Comparison of State Snapshot Transfer Methods`_.
  
You can configure the state snapshot transfer method
with the ``wsrep_sst_method`` variable. For example::

     wsrep_sst_method=rsync_wan

----------------------------------
 Incremental State Transfer (IST)
----------------------------------
.. _`Incremental State Transfer (IST)`:

Galera supports a functionality known as incremental state
transfer. Incremental state transfer means that if:

1. the joining node state UUID is the same as that of the group, and
2. all of the missed writesets can be found in the donor Gcache

then, instead of whole state snapshot, a node will receive the
missing writes ets and catch up with the group by replaying them.

For example, if the local node state is::

    5a76ef62-30ec-11e1-0800-dba504cf2aab:197222

...and the group state is::

     5a76ef62-30ec-11e1-0800-dba504cf2aab:201913

...and if writeset number *197223* is still in the donor's
writeset cache (GCache), it will send write sets *197223*-*201913*
to the joiner instead of the whole state. 

IST can dramatically speed up the remerging of a node
to the cluster. It is also non-blocking on the donor.

Perhaps the most important parameter for IST is the GCache size
on the donor. The bigger it is, the more write sets can be
stored in it and the bigger seqno gaps can be closed with
IST. On the other hand, if the GCache is much bigger than
the state size, serving IST may be less efficient than
sending a state snapshot.

Writeset Cache (GCache)
=======================
.. _`Writeset Cache (GCache)`:

Galera stores write sets in a special cache called Writeset
Cache (GCache). In short, GCache is a memory allocator for
write sets and its primary purpose is to minimize the write
set footprint on the :abbr:`RAM (Random-access memory)`.
Galera also improves the offload writeset storage to disk 

GCache has three types of stores:

1. A permanent in-memory store, where write sets are allocated
   by the default OS memory allocator. This store can be useful
   in systems that have spare RAM. The store has a hard size
   limit. By default, it is disabled (the size is set to 0).
2. A permanent ring-buffer file, which is preallocated on disk
   during cache initialization. This store is intended as the
   main writeset store. By default, its size is 128Mb.
3. An on-demand page store, which allocates memory-mapped page
   files during runtime as necessary. The default page size is
   128Mb, but it can also be bigger if it needs to store a bigger
   writeset. 
  
   The size of the page store is limited by the free disk space.
   By default, page files are deleted when not in use, but a
   limit can be set on the total size of the page files to
   keep. When all other stores are disabled, at least one
   page file is always present on disk.

The allocation algorithm attempts to store write sets in the above
order. If the first store does not have enough space to allocate the
writeset, the allocation algorithm attempts to store it on the next
store. The page store always succeeds, unless the writeset is bigger
than the available disk space.

By default, GCache allocates files in the working directory of
the process, but a dedicated location can be specified (see chapter
:ref:`Galera Parameters <Galera Parameters>`.

.. note:: Since all cache files are memory-mapped, the process may
          appear to use more memory than it actually does.


------------------------------------------------
 Comparison of State Snapshot Transfer Methods
------------------------------------------------
.. _`Comparison of State Snapshot Transfer Methods`:

There is no single best state snapshot transfer method; the method
must be chosen depending on the situation. Fortunately, the choice
only must be done on the receiving node; the donor will serve
whatever is requested, as long as it has support for it.

See the table below for a summary table on the the difference
between the different state snapshot transfer methods:

+------------+----------------+-------------------+-------------------------+------------------+---------------------------------------+
| Method     | Speed          | Blocks the donor? | Available on live node? | Logical/Physical | Requires root access to MySQL server? |
+============+================+===================+=========================+==================+=======================================+
| mysqldump  | :red:`slow`    | :red:`yes`        | yes                     | logical          | both donor and joiner                 |
+------------+----------------+-------------------+-------------------------+------------------+---------------------------------------+
| rsync      | fastest        | :red:`yes`        | :red:`no`               | physical         | none                                  |
+------------+----------------+-------------------+-------------------------+------------------+---------------------------------------+
| xtrabackup | fast           | For a short time  | :red:`no`               | physical         | donor only                            |
+------------+----------------+-------------------+-------------------------+------------------+---------------------------------------+

When comparing the different state snapshot transfer methods,
the division between a logical state snapshot and a physical
state snapshot is important, especially from the perspective
of configuration:

- **Physical state snapshot**

  :green:`Pluses`: Physical state snapshot is the fastest to transfer,
  as by definition it does not involve a server on either end. It
  just physically copies data from the disk at one node to the disk
  on the other. It does not depend on the joining node database being
  in a working condition: it just writes all over it. This is a good
  way to restore a corrupted data directory.

  :red:`Minuses`: Physical state snapshot requires the receptor node
  to have the same data directory layout and the same storage engine
  configuration as the donor. For example, InnoDB should have the same
  file-per-table, compression, log file size and similar settings.
  Furthermore, a server with initialized storage engines cannor receive
  physical state snapshots. This means that:

  - The node in need of a SST must restart the server.
  - The server is inaccessible to the mysql client until
    the SST is complete, since the server cannot perform
    authentication without storage engines.

- **Logical state snapshot**

  :green:`Pluses`: A running server can receive a logical state transfer
  (in fact, only a fully initialized server can receive a logical state
  transfer). Logical state transfer does not require a receptor node
  to have the same configuration as the donor node, allowing to upgrade
  storage engine options. You can, for example, migrate from the Antelope
  to the Barracuda file format, start using compression or resize, or
  place iblog* files to another partition.
  
  :red:`Minuses`: A logical state transfer is as slow as mysqldump. The 
  receiving server must be prepared to accept root connections from
  potential donor nodes and the receiving server must have a
  non-corrupted database.

mysqldump
=============

*Mysqldump* requires the receiving node to have a fully functional
database (which can be empty) and the same root credentials as the
donor has. It also requires root access from other nodes. *Mysqldump*
is several times slower than other methods on sizable databases, but
may be faster if the database is very small (smaller than the log
files, for example). It is also sensitive to the *mysqldump* tool
version; it must be the most recent. It is not uncommon for several
*mysqldump* binaries to be found in the system. *Mysqldump* can fail
if an older *mysqldump* tool version is incompatible with the newer
server.

The main advantage of *mysqldump* is that a state snapshot can be
transferred to a working server. That is, the server can be started
standalone and then be instructed to join a cluster from the MySQL
client command line. It also can be used to migrate from older
database formats to newer. 

Sometimes *mysqldump* is the only option. For example, when upgrading
from a MySQL 5.1 cluster with a built-in InnoDB to MySQL 5.5 with an
InnoDB plugin.

The *mysqldump* script only runs on the sending side and pipes the
*mysqldump* output to the MySQL client connected to the receiving
server.

rsync
=============

*Rsync*-based state snapshot transfer is the fastest. It has all pluses and
minuses of the physical snapshot transfer and, in addition, it blocks
the donor for the whole duration of transfer. However, on terabyte-scale
databases, it was found to be considerably (1.5-2 times) faster than
*xtrabackup*. This is several hours faster. *Rsync* does not depend on
MySQL configuration or root access. This makes it probably the easiest
method to configure.

*Rsync* also has the *rsync-wan* modification that engages the *rsync*
delta transfer algorithm. However, this method is more IO intensive
and should only be used when the network throughput is the bottleneck,
that is usually the case in conjunction with wide area networks.

The *rsync* script runs on both sending and receiving sides. On the
receiving side, it starts the *rsync* in server mode and waits for a
connection from the sender. On the sender side, it starts the *rsync*
in client mode and sends the contents of the MySQL data directory to
the joining node.

The most frequently encountered issue with this method is having
incompatible *rsync* versions on the donor and on the receiving 
server.

xtrabackup
==========

*Xtrabackup*-based state snapshot transfer is probably the most
popular choice. As *rsync*, it has the pluses and minuses of the
physical snapshot. However, *xtrabackup* is a virtually non-blocking
method on the donor. It only blocks the donor for a very short period
of time to copy MyISAM tables, such as system tables. If these tables
are small, the blocking time is very short. This naturally happens at
the cost of speed: *xtrabackup* can be considerably slower than *rsync*.

As *xtrabackup* must copy a large amount of data in the shortest
possible time, it may noticeably degrade the donor performance.

The most frequently encountered problem with *xtrabackup* is its
configuration. *xtrabackup* requires that certain options be set
in the *my.cnf* file (for example ``datadir``) and a local root
access to the donor server. Refer to the *xtrabackup* manual for
more details.


------------------------------------
 Scriptable State Snapshot Transfer
-------------------------------------
.. _`Scriptable State Snapshot Transfer`:

Galera has an interface to customize state snapshot transfer through
an external script. The script assumes that the storage engine
initialization on the receiving node takes place only after the state
transfer is complete. In short, this transfer copies the contents of
the source data directory to the destination data directory (with possible
variations).

As of wsrep API patch level 23.7, SST parameters are named. Individual
scripts can use the *wsrep_sst_common.sh* file, which contains common
functions for parsing argument lists, logging errors, and so on. There
is no constraint on the order or number of parameters. New parameters
can be added and any parameter can be ignored by the script. 

Common Parameters
====================

These parameters are always passed to any state transfer script:

– ``role``
– ``address``
– ``auth``
– ``datadir``
– ``defaults-file``
– ``parent``

Donor-specific Parameters
==========================

These parameters are passed to the state transfer script by the state transfer process:

– ``socket`` |---| The local server (donor) socket for
  communications, if is required.
– ``gtid`` |---| The global transaction ID in format: ``<uuid>:<seqno>``.
– ``bypass`` |---| This parameter specifies whether the actual data
  transfer should be skipped and only the GTID should be passed to
  the receiving server (to go straight to incremental state transfer).

mysqldump-specific Parameters
==============================

These parameters are only passed to the ``wsrep_sst_mysqldump``:

– ``user`` |---| The MySQL user to connect to both remote and local
  servers. The user must be the same on both servers.
– ``password`` |---| MySQL user password.
– ``host`` |---| The remote server (receiver) host address.
– ``port`` |---| The remote server (receiver) port.
– ``local-port`` |---| The local server (donor) port.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
   