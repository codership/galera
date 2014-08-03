==========================
 Monitoring the Cluster
==========================
.. _`monitoring-cluster`:

.. index::
   pair: Parameters; wsrep_notify_cmd
   
In Galera Cluster, you can monitor the status of write-set replication throughout the cluster by using standard wsrep queries in the **mysql** client.  As all status variables that relate to write-set replication are prefixed by ``wsrep``, you can query to display them all:

.. code-block:: mysql

	SHOW VARIABLES LIKE 'wsrep_%';

To monitor for changes in cluster membership and node status, you can use the :ref:`notification-cmd`, which communicates such events to the monitoring agent.



.. note:: You can also use Nagios for monitoring Galera Cluster.  For more information, see `Galera Cluster Nagios Plugin <http://www.fromdual.com/galera-cluster-nagios-plugin-en>`_

---------------------------------
 Checking Cluster Integrity
---------------------------------
.. _`check-cluster-integrity`:

.. index::
   pair: Parameters; wsrep_cluster_state_uuid
.. index::
   pair: Parameters; wsrep_cluster_conf_id
.. index::
   pair: Parameters; wsrep_cluster_size
.. index::
   pair: Parameters; wsrep_cluster_status

When all the nodes in your cluster receive and replicate write-sets from the other nodes, your cluster has integrity.  These status variables tell you if the cluster goes down, becomes partitioned or is experiencing a split-brain situation.

To check cluster integrity, for each node complete the following steps:

1. Check that the node is part of the cluster:

   .. code-block:: mysql

	SHOW VARIABLES LIKE 'wsrep_cluster_state_uuid';

   Each node in the cluster should provide the same value.  If you find a node that carries a different cluster state UUID, that node is not connected to the cluster.

2. Check that the node belongs to the same component:

   .. code-block:: mysql

	SHOW VARIABLES LIKE 'wsrep_cluster_conf_id';

   Each node in the cluster should provide the same value.  If you find a node that carries a different value, this indicates the cluster is partitioned.  Once network connectivity is restored, the value will align itself with the others.

3. On the first node, check the number of nodes in the cluster:

   .. code-block:: mysql

	SHOW VARIABLES LIKE 'wsrep_cluster_size';
	  
   If the value equals the expected number of nodes in the cluster, all nodes are connected to the cluster.
   
   .. note:: You only need to check the cluster size on one node.

4. Check the primary status of the cluster component:

   .. code-block::

	SHOW VARIABLES LIKE 'wsrep_cluster_status';

   The node should return a value of ``Primary``.  Other values indicate that the node is part of a nonoperational component.  This can occur in cases of multiple membership changes and a loss of quorum or in the case of a split-brain condition.

   If you don't find a Primary Component, see :ref:`When There is No Primary Component<no-primary-component>`.

Should these status variables check out and return the desired results on each node, then the cluster is up.  This means that replication is able to occur normally through the cluster.  Next check the nodes to ensure that they are in working order and able to receive write-sets.




^^^^^^^^^^^^^^^
When There is No Primary Component
^^^^^^^^^^^^^^^
.. _no-primary-component:

If no node in the cluster is connected to the :term:`Primary Component` (that is, all nodes belong to the same component, which is a non-primary component), attempt to reset the quorum as explained in chapter :ref:`Resetting the Quorum <Resetting the Quorum>`.

If you cannot reset the quorum, the cluster must be manually rebootstrapped. If this is the case,

1. Shut down all nodes.

2. Restart all nodes starting with the most advanced node. To find out the most advanced node, check the ``wsrep_last_committed`` status variable. 

.. note:: Manual bootstrapping has the downside that *gcache* contents are lost and no IST is possible, which would probably be the fastest state transfer method in this kind of case.

This situation is very unlikely. If, however, there is another primary cluster component, there is a loss of connectivity between the nodes. Troubleshoot the problem and restore connectivity. After restoration, the nodes from the non-primary component will automatically reconnect and resynchronize themselves with the primary component.

---------------------------------
 Checking the Node Status
---------------------------------
.. _`check-node-status`:

.. index::
   pair: Parameters; wsrep_cluster_address

.. index::
   pair: Parameters; wsrep_ready

.. index::
   pair: Parameters; wsrep_connected

.. index::
   pair: Parameters; wsrep_local_state_comment

In addition to monitoring cluster integrity, you can also monitor that status of individual nodes.  This tells you whether the node is receiving and processing the SQL load from cluster write-sets and can indicate if there is a problem that is preventing replication.

To check node status, complete the following steps:

1. Check the node status:

   .. code-block:: mysql

	SHOW VARIABLES LIKE 'wsrep_ready';

   If the value is ``TRUE``, the node can accept SQL load.

2. If the ``wsrep_ready`` value is ``FALSE``, check that the node is connected:

   .. code-block:: mysql

	SHOW VARIABLES LIKE 'wsrep_connected';

   If the value is ``OFF``, the node has not connected to any of the cluster components.  This may relate to misconfiguration.  For instance, if the node uses invalid values for ``wsrep_cluster_address`` or ``wsrep_cluster_name``.
   
   Check the error log for proper diagnostics.

3. If the node is connected, but still cannot accept SQL load, check that the node is part of the Primary Component:

   .. code-block:: mysql

	SHOW VARIABLES LIKE 'wsrep_local_state_comment';
   
   If the the state comment is ``Joining``, ``Waiting for SST``, or ``Joined``, the node is syncing with the cluster.  
   
   In a Primary Component, the state comment typically returns ``Joining``, ``Waiting for SST``, ``Joined``, ``Synced``, or ``Donor``.  In other components, the state comment returns ``Initialized``.  Any other state comments are transient and momentary.

Should each of these status variables check out, then the node is in working order.  It is receiving write-sets from the cluster and replicating them to tables on the local database.



---------------------------------
 Checking the Replication Health
---------------------------------
.. _`check-replication-health`:

.. index::
   pair: Parameters; wsrep_flow_control_paused

.. index::
   pair: Parameters; wsrep_cert_deps_distance

.. index::
   pair: Parameters; wsrep_local_recv_queue_avg

.. index::
   pair: Parameters; wsrep_local_recv_queue_max

.. index::
   pair: Parameters; wsrep_local_recv_queue_min

.. note:: These status variables are differential and reset on every ``SHOW STATUS`` command.  To get the current value, execute the query a second time after about a minute.

Flow control settings will result in a pause being set when the wsrep_local_recv_queue exceeds a threshold. Monitoring the following variables will provide an understanding of the wsrep_local_recv_queue length over the period between status examinations::

    wsrep_local_recv_queue_avg
    wsrep_local_recv_queue_max
    wsrep_local_recv_queue_min

By monitoring for cluster integrity and node status, you can watch for any issues that may prevent or otherwise block replication.  Status variables for monitoring replication health allow you to check for performance issues, identifying problem areas so that you can get the most from your cluster.


To check replication health, complete the following steps:

1. Determine the slave lag:

   .. code-block:: mysql

	SHOW STATUS LIKE 'wsrep_flow_control_paused';

   If the variable range is between ``0.0`` and ``1.0`` it indicates the fraction of time the replication was paused since the last ``SHOW STATUS`` command.  A value of ``1.0`` indicates a complete stop.  You want a value as close to ``0.0`` as possible.
    
   The main ways to improve this value are to increase the ``wsrep_slave_threads`` parameter and to exclude the slow nodes from the cluster.

2. Determine the average distance between the lowest and highest seqno values:

   .. code-block:: mysql

	SHOW STATUS LIKE 'wsrep_cert_deps_distance';

   This provides an average of how many transactions you can apply in parallel.  This provides you with the optimal value for the ``wsrep_slave_threads`` parameter, as there is no reason to assign more slave threads than transactions you can apply in parallel.



---------------------------------
 Detecting Slow Network Issues
---------------------------------
.. _`check-network-issues`:

.. index::
   pair: Parameters; wsrep_local_send_queue_avg

.. index::
   pair: Parameters; wsrep_local_send_queue_max

.. index::
   pair: Parameters; wsrep_local_send_queue_min

If you have a slow network, check the value of the variables below::

    wsrep_local_send_queue_avg
    wsrep_local_send_queue_max
    wsrep_local_send_queue_min

In the even that after all the checks and fine-tuning above, you find that you still have one or more nodes running slow, it is possible that the nodes have encountered an issue themselves in the network.

.. note:: This status variables is differential and reset on every ``SHOW STATUS`` command.  To get the current value, execute the query a second time after about a minute.


To determine if you have a slow network, run the following query:

.. code-block:: mysql

	SHOW STATUS LIKE 'wsrep_local_send_queue_avg';

A high value can indicate a bottleneck on the network link.  If this is the case, the cause can be at any layer, from the physical components to the operating system configuration.

---------------------------
Notification Command
---------------------------
.. _`notification-cm`:

Through the **mysql** client, you can check the status of your cluster, the individual nodes and the health of replication.  But, it can prove counterproductive to log into each node and run these checks.  Galera Cluster provides a notification script that allows you to automate monitoring the cluster.

When you set ``wsrep_notify_cmd`` on a node, the server invokes the Notification Command each time cluster membership or the node's local status changes.  You can use this to configure load balancers, raise alarms and so on.


- ``--status [status]`` This argument indicates the status of the node.

  For a list of available options, see :ref:`Node Status String <node-status>`.


- ``--uuid [state UUID]`` This option indicates the cluster state UUID.


- ``--primary [yes|no]`` This option indicates whether or not the current cluster component that the node belongs to is the Primary Component.


- ``--members [list]`` This option provides a list of the member UUID's.

  For more information on the format of the member list, see :ref:`Member List Format <member-list>`.


- ``--index [n]`` This option indicates the index of the node in the member list, (base 0).


^^^^^^^^^^^^^^^^^^
Node Status Strings
^^^^^^^^^^^^^^^^^^
.. _`node-status`:

The notification command with the ``--status`` option uses the following strings to indicate node status.

- ``Undefined`` Indicates a starting node that is not part of the Primary Component.

- ``Joiner`` Indicates a node in the Primary Component that is receiving a state snapshot transfer.

- ``Donor`` Indicates a node in the Primary Component that is sending a state snapshot transfer.

- ``Joined`` Indicates a node in the Primary Component with a complete state that is catching up with the cluster.

- ``Synced`` Indicates a node that is synchronized with the cluster.

- ``Error([error code if available])``

.. note:: Only those nodes that in the ``Synced`` state accept connections from the cluster.  For more information on node states, see :ref:`Node State Changes <node-state-changes>`.




^^^^^^^^^^^^^^^^^^^^^^^^
Member List Element
^^^^^^^^^^^^^^^^^^^^^^^^
.. _`member-list`:

When the notification command runs on the ``--member`` option, it returns a list for each node that is connected to the cluster component.  The notification command uses the following format for each entry in the list::

	[node UUID] / [node name] / [incoming address]

- ``[node UUID]`` This refers to the unique node ID that it receives automatically from the wsrep Provider.

- ``[node name]`` This refers to the name of the node, as set by the ``wsrep_node_name`` parameter in the configuration file.

- ``[incoming address]`` This refers to the IP address for client connections, as set in the ``wsrep_node_incoming_address`` parameter.



