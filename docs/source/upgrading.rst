====================================
 Upgrading Galera Cluster for MySQL
====================================
.. _`Upgrading Galera Cluster for MySQL`:

This chapter describes three different ways to upgrade
*Galera Cluster*. 

---------------
Rolling Upgrade
---------------
.. _`Rolling Upgrade`:

You can carry out a rolling upgrade on a
*Galera Cluster* by applying
the following steps on each cluster node:

.. note:: Transfer all clients connections from the node
          to be upgraded to the other nodes for the time
          of migration.

1. Shutdown the node
2. Upgrade the software
3. Restart the node
4. Wait until the node gets synchronized with the cluster

.. tip:: If the upgraded node has been or will be part of a
         weighted quorum, set the initial node weight to zero.
         In this way, it can be guaranteed that if the joining
         node fails before it gets synchronized, it does not
         have effect in the quorum computation that follows. 

The main advantage of a rolling upgrade is that if something goes
wrong with the upgrade, the other nodes are still operational and
you have time to troubleshoot the problem.

However, rolling upgrade has some issues which deserve consideration:

- Upgrading an individual node can take some time, during
  which the cluster operates at a lower capacity:
  
  - Unless incremental state transfer is used, the node resorts to
    full state snapshot transfer, which can take a long time depending
    on the database size and state transfer method.
  - During that time, the node will accumulate a very long catch-up
    replication event queue, which it will have to replay to get
    synchronized with the cluster. At the same time, the cluster
    adds more and more events to the queue.

- Unless xtrabackup or rsync+LVM state transfer methods are used,
  the state snapshot donor node will be also blocked for the duration
  of the state transfer. Xtrabackup or rsync+LVM state transfer do not
  block the donor, but may slow it down. In practice, the cluster will
  lack 2 nodes for the duration of state transfer and 1 node for the
  duration of the catch-up phase.
- If there are few nodes in the cluster and it operates close to
  its maximum capacity, taking out 2 nodes can lead to a situation
  where the cluster cannot serve all requests, or execution times
  may increase, making the service less available.
- If there are several nodes in the cluster, it would take a long
  time to upgrade the whole cluster.
- Depending on the load balancing mechanism, you may have to instruct
  the load balancer not to direct requests to the joining and donating
  nodes.
- Every time a new node is joining a cluster, cluster performance
  drops until the node buffer pool warms up. Parallel applying helps
  in this.
  
------------
Bulk Upgrade
------------
.. _`Bulk Upgrade`:

A bulk upgrade upgrades all nodes in an idle cluster to avoid
time-consuming state transfers. Bulk upgrade produces a short
but complete service outage. You can carry out a bulk upgrade
on a *Galera Cluster* as follows:

1. Stop all load on the cluster
2. Shut down all the nodes
3. Upgrade software
4. Restart the nodes. The nodes will merge to the cluster
   without state transfers, in a matter of seconds.
5. Resume the load on the cluster

.. note:: You can carry out steps 2-3-4 on all nodes in parallel,
          therefore reducing the service outage time to virtually
          the time needed for a single server restart.

.. warning:: Always use this method for a two-node cluster upgrade, as
             the rolling upgrade would result in a much longer service
             outage.
 
The main advantage of the bulk upgrade is that, for huge databases, it
is faster and results in better availability than the rolling upgrade.
The main drawback of the bulk upgrade is that it relies on the upgrade
and the restart will be quick. However shutting down an InnoDB may take
up a few minutes (as it flushes dirty pages), and if something goes wrong
during the upgrade, there is hardly any time to troubleshoot and fix the
problem. Therefore, do not upgrade all nodes at once, but try it first
on a single node.


---------------------
Provider-only Upgrade
---------------------
.. _`Provider-only Upgrade`:

.. index::
   pair: Parameters; wsrep_cluster_address

If only a Galera provider upgrade is required, the bulk upgrade can
be further optimized to only take a few seconds. The following is an
example for a 64-bit CentOS (or RHEL):

1. Issue the commands below on every node:

::

    # rpm -e galera
    # rpm -i <new galera rpm>

2. Stop all load on the cluster
3. Issue the commands below on every node:

::

    mysql> SET GLOBAL wsrep_provider='none';
    mysql> SET GLOBAL wsrep_provider='/usr/lib64/galera/libgalera_smm.so';

4. Issue the command below on node 1 (or any node):

::

    mysql> SET GLOBAL wsrep_cluster_address='gcomm://'

5. Issue the command below on the other nodes:

::

    mysql> SET GLOBAL wsrep_cluster_address='gcomm://node1'

6. Resume the load on the cluster

Reloading the provider and connecting to the cluster takes
typically less than 10 seconds; there is virtually no service
outage. 

.. important:: In the provider-only upgrade, the warmed up
               InnoDB buffer pool is fully preserved and the
               cluster will continue to operate at full speed
               as soon as the load is resumed.

