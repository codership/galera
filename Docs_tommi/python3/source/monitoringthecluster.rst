==========================
 Monitoring the Cluster
==========================
.. _`Monitoring the Cluster`:

.. index::
   pair: Parameters; wsrep_notify_cmd

You can monitor *wsrep*-related status variables in
the *Galera Cluster* by using the standard *wsrep* queries. As all *wsrep*-related
status variables are prefixed with *wsrep*, you can query them all by
using the command below::

    mysql> SHOW VARIABLES LIKE 'wsrep_%';

You can also define the notification command ``wsrep_notify_cmd``
to be invoked when the cluster membership or node status changes.
This command can also communicate the event to a monitoring agent.
For more information on the ``wsrep_notify_cmd`` command, see chapter 
:ref:`wsrep_notify_cmd <wsrep_notify_cmd>`.

.. note:: You can also use *Nagios* for monitoring the *Galera Cluster*.

          For more information, see http://www.fromdual.com/galera-cluster-nagios-plugin-en.

---------------------------------
 Checking the Cluster Integrity
---------------------------------
.. _`Checking the Cluster Integrity`:

.. index::
   pair: Parameters; wsrep_cluster_state_uuid
.. index::
   pair: Parameters; wsrep_cluster_conf_id
.. index::
   pair: Parameters; wsrep_cluster_size
.. index::
   pair: Parameters; wsrep_cluster_status

When checking the cluster integrity, the first thing you want to know
is whether the node belongs to the right cluster. You can check this
by checking the value of the variable below::

    wsrep_cluster_state_uuid

This variable value must be the same on all cluster nodes. The nodes
with different ``wsrep_cluster_state_uuid`` values are not connected
to the cluster.

Once you have checked whether the node belongs to the right cluster,
you want to check  whether the node belongs to the same component.
You can check this by checking the value of the variable below::

    wsrep_cluster_conf_id

This variable value must be the same on all cluster nodes. If the nodes
have ``different wsrep_cluster_conf_id`` values, they are partitioned.
This is a temporary condition and should be resolved when network
connectivity between the nodes is restored.

You can also view the number of nodes in the cluster by checking the
value of the variable below::

    wsrep_cluster_size

If the shown number of nodes is equal to the expected number of nodes,
all cluster nodes are connected. You can check this variable on one
node only.

Finally, check the primary status of the cluster component to which
the node is connected to::

    wsrep_cluster_status

If this variable value differs from *Primary*, there is a partition
in the cluster and this component is currently unoperational (due to
multiple membership changes and the loss of quorum). A split-brain
condition is also possible. 

If no node in the cluster is connected to the :term:`Primary Component`
(that is, all nodes belong to the same component, which is a
non-primary component), attempt to reset the quorum as explained in
chapter :ref:`Resetting the Quorum <Resetting the Quorum>`.

If you cannot reset the quorum, the cluster must be manually rebootstrapped.
If this is the case,

1. Shut down all nodes.
2. Restart all nodes starting with the most advanced node. To find
   out the most advanced node, check the ``wsrep_last_committed``
   status variable. 

.. note:: Manual bootstrapping has the downside that *gcache* contents are lost
          and no IST is possible, which would probably be the fastest state transfer
          method in this kind of case.


This situation is very unlikely. If, however, there is another primary
cluster component, there is a loss of connectivity between the nodes.
Troubleshoot the problem and restore connectivity. After restoration,
the nodes from the non-primary component will automatically reconnect
and resynchronize themselves with the primary component.

---------------------------------
 Checking the Node Status
---------------------------------
.. _`Checking the Node Status`:

.. index::
   pair: Parameters; wsrep_cluster_address

.. index::
   pair: Parameters; wsrep_ready

.. index::
   pair: Parameters; wsrep_connected

.. index::
   pair: Parameters; wsrep_local_state_comment

When checking the node status, the first thing you want to know
is whether the node is ready to accept SQL load. You can check this
by checking the value of the variable below::

    wsrep_ready

If the value is *true*, the node can accept SQL load. If not, check
the value of the variable below::

    wsrep_connected

If the value is *OFF*, the node has not yet connected to any of the
cluster components. This may be due to misconfiguration
(for example, the configuration contains an invalid
``wsrep_cluster_address`` and/or ``wsrep_cluster_name``).
Check the error log for proper diagnostics.

If the node is connected but ``wsrep_ready`` = *OFF*,  check
the value of the variable below::

    wsrep_local_state_comment

In a primary component, the variable value is typically
*Joining*, *Waiting for SST*, *Joined*, *Synced* or *Donor*.
If ``wsrep_ready`` = *OFF* and the state comment is *Joining*,
*Waiting for SST* or *Joined*, the node is still syncing with
the cluster.

In a non-primary component, the node state comment should be
*Initialized*. Any other states are transient and momentary.

---------------------------------
 Checking the Replication Health
---------------------------------
.. _`Checking the Replication Health`:

.. index::
   pair: Parameters; wsrep_flow_control_paused

.. index::
   pair: Parameters; wsrep_cert_deps_distance

.. note:: Status variables and variables in the chapters below are
          differential and reset on every ``SHOW STATUS`` command.
          To view the value for the current moment, execute two
          ``SHOW STATUS`` commandson the node with an interval of
          ~1 minute. The output of the last invocation will correspond
          to the current moment.

When checking the replication health, the first thing you want to know
is how much slave lag is slowing down the cluster. You can check this
by checking the value of the variable below::

    wsrep_flow_control_paused

If variable value range is from 0.0 to 1.0 and it indicates the fraction
of time the replication was paused since last the ``SHOW STATUS`` command.
Value 1.0 refers to a complete stop. This value should be as close to 0.0
as possible. The main way to improve the value is to increase the
``wsrep_slave_threads`` value and to exclude the slow nodes out of
cluster.

The optimal value for the ``wsrep_slave_threads``, for its part, is
suggested by the value of the variable below::

    wsrep_cert_deps_distance

This variable indicates how many transactions may be applied in parallel
on average. There is no reason to assign the ``wsrep_slave_threads``
value much higher than this. This value can also be quite high, even in
the hundreds. Use common sense and discretion when you define the value
of ``wsrep_slave_threads``.

---------------------------------
 Detecting Slow Network Issues
---------------------------------
.. _`Detecting Slow Network Issues`:

.. index::
   pair: Parameters; wsrep_local_send_queue_avg

If you have a slow network, check the value of the variable below::

    wsrep_local_send_queue_avg

If the variable value is high, the network link can be the bottleneck.
If this is the case, the cause can be at any layer, from the physical
layer to the operating system configuration.
