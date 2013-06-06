=================
 Galera Benefits
=================
.. _`Galera Benefits`:

Galera is a significant step in terms of improving MySQL
high availability. The MySQL ecosystem has offered users
various ways to achieve high availability, but typically
all alternatives have provided only some of the following
features, but not all, thus making the choice of a high
availability solution an exercise in tradeoffs.

Galera provides all of the following features:

* *True multi-master* |---| Read and write to any node at any time.
* *Synchronous replication* |---| No data is lost at node crash or failover.
* *Tightly coupled* |---| All nodes hold the same state. No diverged data between nodes allowed.
* *Multi-threaded slave* |---| No slave lag or performance overhead. For any workload.
* *No master-slave failover* operations or use of VIP.
* *Hot standby* |---| No downtime during failover (since there is no failover).
* *Automatic node provisioning* |---| No need to manually back up the database and 
  copy it to the new node.
* *Supports InnoDB* storage engine. Compatible with the MySQL installed base.

The result is a high availability solution that is both robust
in terms of data integrity and high performance with instant
failovers.

------------------------------
 Galera Cloud Implementations
------------------------------
.. _`Galera Cloud Implementations`:

Another main benefit of Galera is good cloud support.
Automatic node provisioning makes elastic scale-out
and scale-in operations painless. Galera has proven
to perform extremely well in the cloud, such as when
using multiple small node instances, across multiple
data centers (e.g. AWS zones) or even over Wider Area
Networks.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim: