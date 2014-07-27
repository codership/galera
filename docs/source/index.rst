.. galera documentation master file, created by
   sphinx-quickstart on Sat Apr 20 12:00:55 2013.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

==========================
 Galera Cluster Documentation
==========================


.. index::
   pair: Certification based replication; Descriptions
.. index::
   pair: Virtual synchrony; Descriptions

Galera Cluster is a synchronous multi-master database cluster, based on synchronous replication and Oracle's MySQL/InnoDB.  When Galera Cluster is in use, you can direct reads and writes to any node, and you can lose any individual node without interruption in operations and without the need to handle complex failover procedures.

At a high level, Galera Cluster consists of a database server |---| that is, MySQL, MariaDB or Percona XtraDB |---| that then uses the :term:`Galera Replicator` to manage replication.  To be more specific, the MySQL replication plugin API has been extended to provide all the information and hooks required for true multi-master, synchronous replication.  This extended API is called the Write-Set Replication API, or wsrep API.

Through the wsrep API, Galera Cluster provides certification-based replication.  A transaction for replication, the write-set, not only contains the database rows to replicate, but also includes information on all the locks that were held by the database during the transaction.  Each node then certifies the replicated write-set against other write-sets in the applier queue.  The write-set is then applied, if there are no conflicting locks.  At this point, the transaction is considered committed, after which each node continues to apply it to the tablespace. 

This approach is also called virtually synchronous replication, given that while it is logically synchronous, the actual writing and committing to the tablespace happens independently, and thus asynchronously on each node.


----------------------------------------
Benefits of Galera Cluster
----------------------------------------
.. _`Galera Cluster Benefits`:

Galera Cluster provides a significant improvement in high-availability for the MySQL ecosystem.  The various ways to achieve high-availability have typically provided only some of the features available through Galera Cluster, making the choice of a high-availability solution an exercise in tradeoffs.

The following features are available through Galera Cluster:

- **True Multi-master** Read and write to any node at any time.

- **Synchronous Replication** No slave lag, no data is lost at node crash.

- **Tightly Coupled** All nodes hold the same state. No diverged data between nodes allowed.

- **Multi-threaded Slave** For better performance. For any workload.

- **No Master-Slave Failover Operations or Use of VIP.**

- **Hot Standby** No downtime during failover (since there is no failover).

- **Automatic Node Provisioning** No need to manually back up the database and copy it to the new node.

- **Supports InnoDB.**

- **Transparent to Applications** Required no (or minimal) changes) to the application. 

- **No Read and Write Splitting Needed.** 



The result is a high-availability solution that is both robust in terms of data integrity and high-performance with instant failovers.

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Cloud Implementations with Galera Cluster
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`Galera Cluster Cloud Implementations`:

An additional benefit of Galera Cluster is good cloud support.  Automatic node provisioning makes elastic scale-out and scale-in operations painless.  Galera Cluster has been proven to perform extremely well in the cloud, such as when using multiple small node instances, across multiple data centers |---| AWS zones, for example |---| or even over Wider Area Networks.



------------------
Documentation
------------------

.. toctree::
   :includehidden:
   :maxdepth: 3

   technicaldescription
   gettingstarted
   userguide
   troubleshooting
   reference

- :ref:`genindex`
- :ref:`search`
   


.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
