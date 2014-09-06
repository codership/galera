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

- State Snapshot Transfer (SST), which transfers the entire node state as it is (hence "snapshot").

- Incremental State Transfer (IST), which only transfers the results of transactions missing from the joining node.

For more information on SST and IST, see the sections below.

----------------------------------
 State Snapshot Transfer (SST)
----------------------------------
.. _`State Snapshot Transfer (SST)`:

.. index::
   pair: Parameters; wsrep_sst_method
.. index::
   pair: State Snapshot Transfer methods; State Snapshot Transfer

State Snapshot Transfer (SST) refers to a full data copy from one cluster node (donor) to the joining node (joiner). SST is used when a new node joins the cluster. To get synchronized with the cluster, the new node has to transfer data from a node that is already part of the cluster. In Galera replication, you can choose from two conceptually different ways to transfer a state from one MySQL server to another:

- ``mysqldump`` requires the receiving server to be fully initialized and ready to accept connections *before* the transfer. 

  ``mysqldump`` is a blocking method as the donor node becomes ``READ-ONLY`` while data is being copied from one node to another (SST applies the ``FLUSH TABLES WITH READ LOCK`` command for the donor).

  ``mysqldump`` is also the slowest SST method and, in a loaded cluster, this can be an issue.
  
- ``rsync`` / ``rsync_wan`` copies data files directly. This requires that the receiving server is initialized *after* the transfer.  Methods such as ``rsync``, ``rsync_wan``, ``xtrabackup`` and other methods fall into this category.

  These methods are faster than ``mysqldump``, but they have certain limitations. They can only be used on server startup, the receiving server must be configured very similarly to the donor (for example, the ``innodb_file_per_table`` value must be the same, and so on). 

Some of these methods, for example ``xtrabackup``, can be made non-blocking on donor. These methods are supported through a scriptable SST interface.

.. seealso:: Chapter :ref:`Comparison of State Snapshot Transfer Methods <Comparison of State Snapshot Transfer Methods>`
  
You can configure the state snapshot transfer method
with the ``wsrep_sst_method`` variable. For example:

.. code-block:: ini

     wsrep_sst_method=rsync_wan

----------------------------------
 Incremental State Transfer (IST)
----------------------------------
.. _`Incremental State Transfer (IST)`:

.. index::
   pair: Parameters; wsrep_sst_method
.. index::
   pair: State Snapshot Transfer methods; Incremental State Transfer

Galera Cluster supports a functionality known as Incremental State Transfer.  This requires that:

- The joining node state UUID be the same as that of the group.
- All of the missed write-sets can be found in the donor's write-set cache, (GCache).

When this is the case, instead of the whole State Snapshot, a node receives only the missing write-sets and catches up with the group by replaying them.


For example, say that you have a node in the cluster with a node state of::

    5a76ef62-30ec-11e1-0800-dba504cf2aab:197222

And that the group state of the cluster read::

     5a76ef62-30ec-11e1-0800-dba504cf2aab:201913

If write-set number ``197223`` is still in GCache, the donor sends commits ``197223`` through ``201913`` to the joiner instead of the whole state.

IST can dramatically speed up the remerging of a node to the cluster. It is also non-blocking on the donor.

Perhaps the most important parameter for IST is the GCache size on the donor. The bigger it is, the more write-sets can be stored in it, and the bigger seqno gaps can be closed with IST. On the other hand, if the GCache is much bigger than the state size, serving IST may be less efficient than sending a state snapshot.

Write-set Cache (GCache)
=======================
.. _`Writeset Cache (GCache)`:
.. index::
   pair: GCache; Descriptions
.. index::
   pair: Writeset Cache; Descriptions

Galera Cluster stores write sets in a special cache called Write-set Cache (GCache).  GCache is a memory allocator for write-sets and its primary purpose is to minimize the write-set footprint on the :abbr:`RAM (Random-access memory)`.  Galera Cluster also improves the offload write-set storage to disk.

GCache has three types of stores:

- A permanent in-memory store, where write-sets are allocated by the default memory allocator for the operating system. This store can be useful in systems that have spare RAM. The store has a hard size limit. By default, it is disabled.

- A permanent ring-buffer file, which is preallocated on disk during cache initialization. This store is intended as the main write-set store. By default, its size is 128Mb.

- An on-demand page store, which allocates memory-mapped page files during runtime as necessary. The default page size is 128Mb, but it can also be bigger if it needs to store a larger write-set. 
  
  The size of the page store is limited by the free disk space. By default, page files are deleted when not in use, but a limit can be set on the total size of the page files to keep. When all other stores are disabled, at least one page file is always present on disk.
   
   .. seealso:: GCache related parameter descriptions in chapter
                :ref:`Galera Parameters <Galera Parameters>`

The allocation algorithm attempts to store write sets in the above order. If the first store does not have enough space to allocate the write-set, the allocation algorithm attempts to store it on the next store. The page store always succeeds, unless the writeset is larger than the available disk space.

By default, GCache allocates files in the working directory of the process, but a dedicated location can be specified (see chapter :ref:`Galera Parameters <Galera Parameters>`.

.. note:: Since all cache files are memory-mapped, the process may
          appear to use more memory than it actually does.
