====================================
 Upgrading Galera Cluster
====================================
.. _`upgrading-galera`:


You have three methods available in upgrading Galera Cluster:

- :ref:`Rolling Upgrade <rolling-upgrade>` Where you upgrade each node one at a time. 
- :ref:`Bulk Upgrade <bulk-upgrade>` Where you upgrade all nodes together.
- :ref:`Provider Upgrade <provider-upgrade>` Where you only upgrade the Galera Replication Plugin.

There are advantages and disadvantages to each method.  For instance, while a rolling upgrade may prove time consuming, the cluster remains up.  Similarly, while a bulk upgrade is faster, problems can result in longer outages.  You must choose the best method to implement in upgrading your cluster.


-----------------
Rolling Upgrade
-----------------
.. _`rolling-upgrade`:



When you need the cluster to remain live and do not mind the time it takes to upgrade each node, use rolling upgrades.

In rolling upgrades, you take each node down individually, upgrade its software and then restart the node.  When the node reconnects, it brings itself back into sync with the cluster, as it would in the event of any other outage.  Once the individual finishes syncing with the cluster, you can move to the next in the cluster.

The main advantage of a rolling upgrade is that in the even that something goes wrong with the upgrade, the other nodes remain operational, giving you time to troubleshoot the problem.

Some of the disadvantages to consider in rolling upgrades are:

- **Time Consumption** Performing a rolling upgrade can take some time, longer depending on the size of the databases and the number of nodes in the cluster, during which the cluster operates at a diminished capacity.

  Unless you use Incremental State Transfers, as you bring each node back online after an upgrade, it initiates a full State Snapshot Transfer, which can take a long time to process on larger databases and slower state transfer methods.

  During the State Snapshot Transfer, the node continues to accumulate catch-up in the replication event queue, which it will then have to replay to synchronize with the cluster.  At the same time, the cluster is operational and continues to add further replication events to the queue. 
  
- **Blocking Nodes** When the node comes back online, if you use **mysqldump** for State Snapshot Transfers, the donor node remains blocked for the duration of the transfer.  In practice, this means that the cluster is short two nodes for the duration of the state transfer, one for the donor node and one for the node in catch-up.  

  Using **xtrabackup** or **rsync** with the LVM state transfer methods, you can avoid blocking the donor, but doing so may slow the donor node down.

  .. note:: Depending on the load balancing mechanism, you may have to configure the load balancer not to direct requests at joining and donating nodes.
  
- **Cluster Availability** Taking down nodes for a rolling upgrade can greatly diminish cluster performance or availability, such as if there are too few nodes in the cluster to begin with or where the cluster is operating at its maximum capacity.  

  In such cases, losing access to two nodes during a rolling upgrade can create situations where the cluster can no longer serve all requests made of it or where the execution times of each request increase to the point where services become less available.

- **Cluster Performance** Each node you bring up after an upgrade, diminishes cluster performance until the node buffer pool warms back up.  Parallel applying can help with this.

To perform a rolling upgrade on Galera Cluster, complete the following steps for each node:

.. note:: Transfer all client connections from the node you are upgrading to the other nodes for the duration of this procedure.

1. Shut down the node.

2. Upgrade the software.

3. Restart the node.

Once the node finishes synchronizing with the cluster and completes its catch-up, move on tot he next node in the cluster.  Repeat the procedure until you have upgraded all nodes in the cluster.

.. tip:: If you are upgraded a node that is or will be part of a weighted quorum, set the initial node weight to zero.  This guarantees that if the joining node should fail before it finishes synchronizing, it will not affect any quorum computations that follow.




------------
Bulk Upgrade
------------
.. _`bulk-upgrade`:

When you want to avoid time-consuming state transfers and the slow process of upgrading each node, one at a time, use a bulk upgrade.

In bulk upgrades, you take all of the nodes down in an idle cluster, perform the upgrades, then bring the cluster back online.  This allows you to upgrade your cluster quickly, but does mean a complete service outage for your cluster.

.. warning:: Always use bulk upgrades when using a two-node cluster, as the rolling upgrade would result in a much longer service outage.

The main advantage of bulk upgrade is that when you are working with huge databases, it is much faster and results in better availability than rolling upgrades.

The main disadvantage is that it relies on the upgrade and restart being quick.  Shutting down InnoDB may take a few minutes as it flushes dirty pages.  If something goes wrong during the upgrade, there is little time to troubleshoot and fix the problem.  

.. note:: To minimize any issues that might arise from an upgrade, do not upgrade all of the nodes at once.  Rather, run the upgrade on a single node first.  If it runs without issue, upgrade the rest of the cluster.  

To perform a bulk upgrade on Galera Cluster, complete the following steps:

1. Stop all load on the cluster

2. Shut down all the nodes

3. Upgrade software

4. Restart the nodes. The nodes will merge to the cluster without state transfers, in a matter of seconds.

5. Resume the load on the cluster

.. note:: You can carry out steps 2-3-4 on all nodes in parallel, therefore reducing the service outage time to virtually the time needed for a single server restart.


---------------------
Provider-only Upgrade
---------------------
.. _`provider-upgrade`:

.. index::
   pair: Parameters; wsrep_cluster_address

When you only need to upgrade the Galera provider, you can further optimize the bulk upgrade to only take a few seconds.

.. important:: In provider-only upgrade, the warmed up InnoDB buffer pool is fully preserved and the cluster continues to operate at full speed as soon as you resume the load.

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Upgrading Galera Replication Plugin
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`upgrade-plugin`:

If you installed Galera Cluster for MySQL using the binary package from `Launchpad <https://launchpad.net/galera>`_, you can upgrade the Galera Replication Plugin from the same.

To update the Galera Replicator Plugin for Galera Cluster for MySQL, complete the following steps on each node in the cluster:

1. Go to `Galera Replicator <https://launchpad.net/galera>`_ and download the new version of the Galera Replicator Plugin, referred to hereafter as ``galera-new``.

2. Remove the existing Galera Replicator Plugin.

   If you are using an RPM-based distribution of Linux, run the following command:
   
   .. code-block:: console
   
      $ rpm -e galera
   
   If you are using a Debian-based distribution of Linux, run the following command:
   
   .. code-block:: console
   
      $ dpkg -r galera

3. Install the new Galera Replicator package.

   If you are using an RPM-based distribution of Linux, run the following command:
   
   .. code-block:: console
   
      $ rpm -i /path/to/galera-new.rpm
   
   If you are using a Debian-based distribution of Linux, run the following command:
   
   .. code-block:: console
   
      $ dpkg -i /path/to/galera-new.deb


4. Install the Galera Replicator package:

   .. code-block:: console

      $ dpkg -i galera.deb

This upgrades the binary package for the Galera Replicator Plugin.  Once this process is complete, you can move on to updating the cluster to use the newer version of the plugin.

If you use Galera Cluster for MariaDB or for Percona XtraDB Cluster and you installed from a binary package through the MariaDB or Percona repositories, you can upgrade the provider through your package manager.

To upgrade the Galera Replicator Plugin on an RPM-based Linux distribution, run the following command for each node in the cluster:

   .. code-block:: console
   
      $ yum update galera

To upgrade the Galera Replicator Plugin on a Debian-based Linux distribution, run the following commands for each node in the cluster:

   .. code-block:: console
   
      $ apt-get update
      $ apt-get upgrade galera

When **apt-get** or **yum** finish, you will have the latest version of the Galera Replicator Plugin available on the node.  Once this process is complete, you can move on to updating the cluster to use the newer version of the plugin.

^^^^^^^^^^^^^^^^^^^^^^^^
Updating Galera Cluster
^^^^^^^^^^^^^^^^^^^^^^^^

After you upgrade the Galera Replicator Plugin package on each node in the cluster, you need to run a bulk upgrade to switch the cluster over to the newer version of the plugin.

1. Stop all load on the cluster.

2. For each node in the cluster, issue the following queries:

   .. code-block:: mysql
   
      SET GLOBAL wsrep_provider='none';
      SET GLOBAL wsrep_provider='/usr/lib64/galera/libgalera_smm.so';

3. One any one node in the cluster, issue the following query:

   .. code-block:: mysql
   
      SET GLOBAL wsrep_cluster_address='gcomm://';

4. For every other node in the cluster, issue the following query:

   .. code-block:: mysql
   
      SET GLOBAL wsrep_cluster_address='gcomm://node1addr';
   
   For ``node1addr``, use the address of the node in step 3.

5. Resume the load on the cluster.

Reloading the provider and connecting it to the cluster typically takes less than ten seconds, so there is virtually no service outage.




