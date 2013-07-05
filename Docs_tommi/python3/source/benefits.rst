=========================
 Galera Cluster Benefits
=========================
.. _`Galera Cluster Benefits`:

*Galera Cluster* is a significant step in terms of improving MySQL
high availability. The MySQL ecosystem has offered users
various ways to achieve high availability, but typically
all alternatives have provided only some of the following
features, but not all, thus making the choice of a high
availability solution an exercise in tradeoffs.

*Galera Cluster* provides all of the following features:

* *True multi-master* |---| Read and write to any node at any time.
* *Synchronous replication* |---| No slave lag, no data is lost at node crash.
* *Tightly coupled* |---| All nodes hold the same state. No diverged data between nodes allowed.
* *Multi-threaded slave* |---| For better performance. For any workload.
* *No master-slave failover* operations or use of VIP.
* *Hot standby* |---| No downtime during failover (since there is no failover).
* *Automatic node provisioning* |---| No need to manually back up the database and 
  copy it to the new node.
* *Supports InnoDB* storage engine.
* *Tranparent to applications* |---| Required no (or minimal) changes)
  to the application. 
* No read and write splitting needed. 



The result is a high availability solution that is both robust
in terms of data integrity and high performance with instant
failovers.

----------------------------------------
 Galera Cluster Cloud Implementations
----------------------------------------
.. _`Galera Cluster Cloud Implementations`:

Another main benefit of *Galera Cluster* is good cloud support.
Automatic node provisioning makes elastic scale-out
and scale-in operations painless. *Galera Cluster* has proven
to perform extremely well in the cloud, such as when
using multiple small node instances, across multiple
data centers (for example, AWS zones) or even over Wider Area
Networks.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
