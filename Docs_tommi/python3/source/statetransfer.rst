.. raw:: html

    <style> .red {color:red} </style>

.. raw:: html

    <style> .green {color:green} </style>

.. role:: red
.. role:: green

==========================
 Node Provisioning
==========================
.. _`Node Provisioning`:

There are two different node provisioning methods:

- State Snapshot Transfer (SST), which transfers the entire
  node state as it is (hence "snapshot").
- Incremental State Transfer (IST), which only transfers the
  results of transactions missing from the joining node.

These methods are described in the chapters below.

----------------------------------
 State Snapshot Transfer (SST)
----------------------------------
.. _`State Snapshot Transfer (SST)`:

.. index::
   pair: Parameters; wsrep_sst_method
.. index::
   pair: State Snapshot Transfer methods; State Snapshot Transfer

State Snapshot Transfer (SST) refers to a full data copy from
one cluster node (donor) to the joining node (joiner). 
SST is used when a new node joins the cluster. To get synchronized
with the cluster, the new node has to transfer data from a node
that is already part of the cluster. In Galera replication, you
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

.. seealso:: Chapter :ref:`Comparison of State Snapshot Transfer Methods <Comparison of State Snapshot Transfer Methods>`
  
You can configure the state snapshot transfer method
with the ``wsrep_sst_method`` variable. For example::

     wsrep_sst_method=rsync_wan

----------------------------------
 Incremental State Transfer (IST)
----------------------------------
.. _`Incremental State Transfer (IST)`:

.. index::
   pair: Parameters; wsrep_sst_method
.. index::
   pair: State Snapshot Transfer methods; Incremental State Transfer

*Galera Cluster* supports a functionality known as incremental state
transfer. Incremental state transfer means that if:

1. the joining node state UUID is the same as that of the group, and
2. all of the missed writesets can be found in the donor Gcache

then, instead of whole state snapshot, a node will receive the
missing write sets and catch up with the group by replaying them.

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
stored in it, and the bigger seqno gaps can be closed with
IST. On the other hand, if the GCache is much bigger than
the state size, serving IST may be less efficient than
sending a state snapshot.

Writeset Cache (GCache)
=======================
.. _`Writeset Cache (GCache)`:
.. index::
   pair: GCache; Descriptions
.. index::
   pair: Writeset Cache; Descriptions

*Galera Cluster* stores write sets in a special cache called Writeset
Cache (GCache). In short, GCache is a memory allocator for
write sets and its primary purpose is to minimize the write
set footprint on the :abbr:`RAM (Random-access memory)`.
*Galera Cluster* also improves the offload writeset storage to disk.

GCache has three types of stores:

1. A permanent in-memory store, where write sets are allocated
   by the default OS memory allocator. This store can be useful
   in systems that have spare RAM. The store has a hard size
   limit. By default, it is disabled.
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
   
   .. seealso:: GCache related parameter descriptions in chapter
                :ref:`Galera Parameters <Galera Parameters>`

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

------------------------------------
 Scriptable State Snapshot Transfer
------------------------------------
.. _`Scriptable State Snapshot Transfer`:

*Galera Cluster* has an interface to customize state snapshot transfer through
an external script. The script assumes that the storage engine
initialization on the receiving node takes place only after the state
transfer is complete. In short, this transfer copies the contents of
the source data directory to the destination data directory (with possible
variations).

As of *wsrep API* patch level 23.7, SST parameters are named. Individual
scripts can use the *wsrep_sst_common.sh* file, which contains common
functions for parsing argument lists, logging errors, and so on. There
is no constraint on the order or number of parameters. New parameters
can be added and any parameter can be ignored by the script. 

Common Parameters
====================

These parameters are always passed to any state transfer script:

- ``role``
- ``address``
- ``auth``
- ``datadir``
- ``defaults-file``
- ``parent``

Donor-specific Parameters
==========================

These parameters are passed to the state transfer script by the state transfer process:

- ``socket`` |---| The local server (donor) socket for
  communications, if is required.
- ``gtid`` |---| The :term:`Global Transaction ID` in format: ``<uuid>:<seqno>``.
- ``bypass`` |---| This parameter specifies whether the actual data
  transfer should be skipped and only the GTID should be passed to
  the receiving server (to go straight to incremental state transfer).

mysqldump-specific Parameters
==============================

These parameters are only passed to the ``wsrep_sst_mysqldump``:

- ``user`` |---| The MySQL user to connect to both remote and local
  servers. The user must be the same on both servers.
- ``password`` |---| MySQL user password.
- ``host`` |---| The remote server (receiver) host address.
- ``port`` |---| The remote server (receiver) port.
- ``local-port`` |---| The local server (donor) port.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
   
