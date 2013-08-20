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

If the state of a new or failed node differs from the state of
the cluster :term:`Primary Component` it needs to be synchronized. As a result,
new node provisioning and failed node recovery are essentially
the same process of joining a node to the cluster
:abbr:`PC (Primary Component)`.

The initial node state ID is read from the *grastate.txt*
file in ``wsrep_data_dir``, where it is saved every time
the node is gracefully shut down. If the node crashes in
the :term:`Total Order Isolation` mode, its database state is
unknown and its initial *Galera Cluster*
node state is undefined
(``00000000-0000-0000-0000-000000000000:-1``). [1]_

.. [1] In normal transaction processing, only the ``seqno`` part
       of the GTID remains undefined (``-1``), and the ``UUID``
       part remains valid. In this case, the node can be recovered
       through IST.

When a node joins the primary component, it compares its
state ID to that of the :abbr:`PC (Primary Component)` and
if they do not match, the node requests state transfer
from the cluster.

There are two possibilities to select the state transfer donor:

- Automatic |---| The group communication layer determines
  the state donor from the available members of the primary
  component.
- Manual |---| The state donor name is specified with the
  ``wsrep_sst_donor`` parameter on startup.

In the latter case, if a node with that name is not a part of
the primary component, state transfer fails and the joining node
aborts. Use the same donor name as set in the ``wsrep_node_name``
parameter on the donor node.

.. note:: State transfer is a heavy operation not only on the
          joining node, but also on the donor. The state donor may
          not be able to serve client requests. Thus, when possible,
          select the donor manually, based on network proximity.
          Configure the load balancer to transfer client connections
          to the other nodes for the duration of state transfer.

During the state transfer the joining node caches writesets received
from other nodes in a *slave queue* and applies them after the
state transfer is over, to catch up with the current primary
component state. Since the state snapshot always has a
state ID, it is easy to determine which writesets are already
contained in the snapshot and should be discarded.

During the catch-up phase, flow control ensures that the slave
queue gets shorter (that is, the cluster replication rate will
be limited to the writeset application rate on the catching node).
However, there is no guarantee on how soon the node will catch up.
When the node catches up, its status becomes ``SYNCED`` and
it will accept client connections.

------------------------------------------------
 Comparison of State Snapshot Transfer Methods
------------------------------------------------
.. _`Comparison of State Snapshot Transfer Methods`:

.. index::
   pair: State Snapshot Transfer methods; Comparison of

There are two different node provisioning methods:

- State Snapshot Transfer (SST), which transfers the entire
  node state as it is (hence "snapshot").
- Incremental State Transfer (IST), which only transfers the
  results of transactions missing from the joining node.

You can choose the SST method (*mysqldump*, *rsync*, or
*xtrabackup*), whereas IST will be automatically chosen
by the donor node, when it is available.  The SST methods
are compared in this chapter.

There is no single best state snapshot transfer method; the method
must be chosen depending on the situation. Fortunately, the choice
only must be done on the receiving node; the donor will serve
whatever is requested, as long as it has support for it.

See the table below for a summary on the the differences
between the state snapshot transfer methods:

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
  
  :red:`Minuses`: A logical state transfer is as slow as *mysqldump*. The 
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

.. index::
   single: my.cnf

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


.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
