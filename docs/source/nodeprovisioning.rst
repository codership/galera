.. raw:: html

    <style> .red {color:red} </style>

.. raw:: html

    <style> .green {color:green} </style>

.. role:: red
.. role:: green

================================
 Node Provisioning and Recovery
================================
.. _`Node Provisioning and Recovery`:

.. index::
   pair: Parameters; wsrep_data_dir
.. index::
   pair: Parameters; wsrep_sst_donor
.. index::
   pair: Parameters; wsrep_node_name
.. index::
   single: Total Order Isolation

When the state of a new or failed node differs from that of the cluster's :term:`Primary Component`, the new or failed node must be synchronized with the cluster.  Because of this, the provisioning of new nodes and the recover of failed nodes are essentially the same process as that of joining a node to the cluster Primary Component.

Galera reads the initial node state ID from the **grastate.txt** file, found in the directory assigned by the ``wsrep_data_dir`` parameter.  Each time the node gracefully shuts down, Galera saves to this file.  

In the event that the node crashes while in :term:`Total Order Isolation` mode, its database state is unknown and its initial node state remains undefined::

	00000000-0000-0000-0000-000000000000:-1

.. note:: In normal transaction processing, only the :term:`seqno` part of the GTID remains undefined, (that is, with a value of ``-1``.  The UUID, (that is, the remainder of the node state), remains valid.  In such cases, you can recover the node through an :term:`Incremental State Transfer`. 

---------------------------
How Nodes Join the Cluster
---------------------------

When a node joins the cluster, it compares its own state ID to that of the Primary Component.  If the state ID does not match, the joining node requests a state transfer from the cluster.

There are two options available to determining the state transfer donor:

- **Automatic** When the node attempts to join the cluster, the group communication layer determines the state donor it should use from those members available in the Primary Component.

- **Manual** When the node attempts to join the cluster, it uses the ``wsrep_sst_donor`` parameter to determine which state donor it should use.  If it finds that the state donor it is looking for is not part of the Primary Component, the state transfer fails and the joining node aborts.  For ``wsrep_sst_donor``, use the same name as you use on the donor node for the ``wsrep_node_name`` parameter.

.. note:: A state transfer is a heavy operation.  This is true not only for the joining node, but also for the donor.  In fact, a state donor may not be able to serve client requests.  

	Thus, whenever possible: manually select the state donor, based on network proximity and configure the load balancer to transfer client connections to other nodes in the cluster for the duration of the state transfer.

When a state transfer is in process, the joining node caches write-sets that it receives from other nodes in a slave queue.  Once the state transfer is complete, it applies the write-sets from the slave queue to catch up with the current Primary Component state.  Since the state snapshot carries a state ID, it is easy to determine which write-sets the snapshot contains and which it should discard.

During the catch-up phase, flow control ensures that the slave queue shortens, (that is, it limits the cluster replication rates to the write-set application rate on the node that is catching up).  

While there is no guarantee on how soon a node will catch up, when it does the node status updates to ``SYNCED`` and it begins to accept client connections.

-------------------
State Transfers
-------------------
.. _`state-transfer`:

There are two types of state transfers available to bring the node up to date with the cluster:

- **State Snapshot Transfer (SST)** Where donor transfers to the joining node a snapshot of the entire node state as it stands.

- **Incremental State Transfer (IST)** Where the donor only transfers the results of transactions missing from the joining node.

When using automatic donor selection, starting in Galera Cluster version 3.6, the cluster decides which state transfer method to use based on availability.

- If there are no nodes available that can safely perform an incremental state transfer, the cluster defaults to a state snapshot transfer.

- If there are nodes available that can safely perform an incremental state transfer, the cluster prefers a local node over remote nodes to serve as the donor.

- If there are no local nodes available that can safely perform an incremental state transfer, the cluster chooses a remote node to serve as the donor.

- Where there are several local or remote nodes available that can safely perform an incremental state transfer, the cluster chooses the node with the highest seqno to serve as the donor.


--------------------------------------
Comparison of State Transfer Methods
--------------------------------------
.. _`state-transfer-methods`:

.. index::
   pair: State Snapshot Transfer methods; Comparison of


When performing state snapshot transfers, you can choose the method Galera uses in the transfer, (**mysqldump**, **rsync**, or **xtrabackup**).  When performing incremental snapshot transfers, the donor node determines the method Galera uses.

This chapter covers state snapshot transfers.


+------------------------------+-----------------+---------------+------------------------+------------------+------------------------------------------+
| Method                       | Speed           | Blocks Donor? | Available on Live Node | Type             | Requires root access to database server? |
+==============================+=================+===============+========================+==================+==========================================+
| :ref:`mysqldump<mysqldump>`  | :red:`slow`     | :green:`yes`  | :green:`yes`           | logical           | donor and joiner                        |
+------------------------------+-----------------+---------------+------------------------+-------------------+-----------------------------------------+
| :ref:`rsync<rsync>`          | :green:`fastest`| :green:`yes`  | :red:`no`              | physical          | none                                    |
+------------------------------+-----------------+---------------+------------------------+-------------------+-----------------------------------------+
| :ref:`xtrabackup<xtrabackup>`| :green:`fast`   | briefly       | :red:`no`              | physical          | donor only                              |
+------------------------------+-----------------+---------------+------------------------+-------------------+-----------------------------------------+

There is no one best State Snapshot Transfer method.  You must choose what method to use based on the situation.  Fortunately, you need only set the method on the receiving node.  So long as the donor has support, it serves the transfer in whatever method the joiner node requests.

There are two types of state snapshot transfers to consider from the perspective of configuration:


**Physical State Snapshot**
  
:green:`Pros`:  A physical state snapshot is the fastest to transfer, as it does not involve a server on either side.  The transfer physically copies data from the disk of one node to the disk of the other.  It does not need the joining database in a working condition.  The transfer overwrites whatever was previously there.
  
This is a good method to use in restoring a corrupted data directory.

:red:`Cons`:  A physical state snapshot requires the receptor node to have the same data directory layout and the same storage engine configuration as the donor.  For example, InnoDB must have the same file-per-table, compression, log file size and similar settings.

Furthermore, a server with initialized storage engines cannot receive a physical state snapshot.  This means that:

- The node in need of a state snapshot transfer must restart the database server.

- The database server remains inaccessible to the client until the state snapshot transfer is complete, since the server cannot perform authentication without the storage engines.

**Logical State Snapshot**

:green:`Pros`: A logical state transfer can be used on a running server.  In fact, only a fully initialized server can receive a logical state snapshot transfer.  It does not require a receptor node to have the same configuration as the donor node.  This allows you to upgrade storage engine options.  

For example, with a logical state snapshop transfer, you can migrate from the Antelope to the Barracuda file format, start using compression resize, or move iblog* files to another partition. 

:red:`Cons`: A logical state snapshot is as slow as **mysqldump**.  It requires that you configure the receiving server to accept root connections from potential donor nodes.  The receiving server must have a non-corrupted database.


^^^^^^^^^^^^^^^^^
mysqldump
^^^^^^^^^^^^^^^^^
.. _mysqldump:

The main advantage of **mysqldump** is that you can transfer a state snapshot to a working server.  That is, you start the server standalone and then instruct it to join a cluster from the database client command line.  

You can also use it to migrate from an older database format to a newer one.

**mysqldump** requires that the receiving node have a fully functional database, which can be empty, and the same root credentials as the donor.  It also requires root access from the other nodes.

This method is several times slower than the others on sizable databases, but it may prove faster in the case of very small databases.  For instance, when the database is smaller than the log files.

.. note:: State snapshot transfers that use **mysqldump** are sensitive to the version of the tool each node uses.  It is not uncommon for a given system to have installed several versions.  A state snapshot transfer can fail if the version one node uses is older and incompatible with the newer server.

On occasion, **mysqldump** is the only option available.  For instance, if you upgrade from a MySQL 5.1 cluster with the built-in InnoDB to MySQL 5.5 with the InnoDB plugin.

The **mysqldump** script only runs on the sending side.  The output from the script gets piped to the MySQL client that connects to the receiving server.

For more information, see the `mysqldump Documentation <http://dev.mysql.com/doc/refman/5.6/en/mysqldump.html>`_.

^^^^^^^^^^^^^^^^^
rsync
^^^^^^^^^^^^^^^^^
.. _rsync:

The fastest state snapshot transfer method is **rsync**.  It carries all the advantages and disadvantages of the physical snapshot transfer method with the added bonus of blocking the donor for the duration of the transfer.  **rsync** does not require database configuration or root access, which makes it easier to configure.

On terabyte-scale databases, it was found to be considerably faster, (1.5 to 2 times faster), than **xtrabackup**.  This means transfer times on larger databases can process several hours faster.

Additionally, **rsync** features the rsync-wan modification, which engages the **rsync** delta transfer algorithm.  However, given that this makes it more I/O intensive, you should only use it when the network throughput is the bottleneck, which is usually the case with wide area networks.

.. note:: The most common issue encountered with this method is due to incompatibilities between the versions of **rsync** on the donor and joining nodes.

The **rsync** script runs on both sending and receiving sides.  On the receiving side, it starts **rsync** in server-mode and waits for a connection from the sender.  On the sender side, it starts **rsync** in client-mode and sends the contents of the data directory to the receiving node.

For more information, see the `Rsync Documentation <http://rsync.samba.org/>`_.


^^^^^^^^^^^^^^^^^
xtrabackup
^^^^^^^^^^^^^^^^^
.. _xtrabackup:
.. index::
   single: my.cnf

The most popular method for state snapshot transfers is **xtrabackup**.  It carries all the advantages and disadvantages of physical state snapshot transfers, but is virtually non-blocking on the donor.  

**xtrabackup** only blocks the donor for the short period of time it takes to copy MyISAM tables, (the system tables, for instance).  If these tables are small, the blocking time is very short.  However, this comes at the cost of speed: **xtrabackup** state snapshot transfers can be considerably slower than those that use **rsync**.

Given that **xtrabackup** must copy a large amount of data in the shortest time possible, it may noticeably degrade donor performance.


.. note:: The most common issue encountered with this method is due to it configuration.  **xtrabackup** requires that you set certain options in the configuration file, (``my.cnf`` or ``my.ini``, depending on your build) and a local root access to the donor server.

For more information, see the `Percona XtraBackup User Manual <https://www.percona.com/doc/percona-xtrabackup/2.1/manual.html?id=percona-xtrabackup:xtrabackup_manual>`_.


.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
