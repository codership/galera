==================================
 Node Failure and Recovery
==================================
.. _`Node Failure and Recovery`:

--------------------
 Single Node Failure
--------------------
.. _`Single Node Failure`:

A single *Galera Cluster* cluster node can
fail to operate for a variety of reasons, such as:

- A hardware failure
- A software crash
- Loss of network connectivity

All of these causes are generalized behind the concept of a node
failure.

Single Node Failure Detection
=============================

.. index::
   pair: Parameters; evs.keepalive_period

.. index::
   pair: Parameters; evs.inactive_check_period

.. index::
   pair: Parameters; evs.suspect_timeout

.. index::
   pair: Parameters; evs.inactive_timeout

.. index::
   pair: Parameters; evs.consensus_timeout

The only sign of a node failure is a loss of connection to the
node process as seen by another node. The node is considered failed
when it is no longer a member of the cluster :term:`Primary Component`, that
is, when the members of the primary component no longer see it.
From the perspective of the failed node (unless it has crashed
itself), it has lost connection to the primary component.

Third-party node monitoring tools, such as ping/Heartbeat/Pacemaker,
may be grossly off in their estimates on the node failure, as they
do not participate in the *Galera Cluster* group communication and are not
aware of the primary component. Monitor the *Galera Cluster* node status
only by polling the ``wsrep_local_state`` status variable or use
a notification script.

.. seealso: Chapter :ref:`Monitoring the Cluster <Monitoring the Cluster>`

Node connection liveness is determined from the last time a network
packet was received from the node. This is checked every
``evs.inactive_check_period``. If the node has no messages to send
for more than ``evs.keepalive_period``, it will emit heartbeat beacons
instead. If no packets were received from the node for the
``evs.suspect_timeout``, the node is declared suspected. When all
members of the component see the node as suspected, it is pronounced
inactive (failed). If no messages were received from the node for
more than ``evs.inactive_timeout``, it is pronounced inactive
regardless of the consensus. The component stays non-operational
until all members agree on the membership. If the members cannot
reach consensus on the liveness of a node, the network is too
unstable for the cluster to operate.

The relation between the option values is::

    evs.keepalive_period <= evs.inactive_check_period <= evs.suspect_timeout <= evs.inactive_timeout <= evs.consensus_timeout

.. note:: An unresponsive node, which fails to send messages or
          heartbeat beacons on time due to, for example, heavy
          swapping, may also be pronounced failed. Thus it will not
          lock the operation of the rest of the cluster. If such
          behaviour is undesirable, increase the timeouts.

Trade-Off Between Availability and Partition Tolerance
======================================================

Within the `CAP theorem`_, *Galera Cluster* emphasizes data safety and
consistency, which leads to a trade-off between cluster availability
and partition tolerance. To be more specific, in unstable networks
(such as :abbr:`WAN (Wide Area Network)`) low
``evs.suspect_timeout``/``evs.inactive_timeout`` values may result
in false node failure detections, whereas higher values will result
in longer availability outages in the case of a real node failure.
Essentially, the ``evs.suspect_timeout`` defines the minimum time
needed to detect a failed node, during which the cluster will be
unavailable due to the consistency constraint.

.. _CAP theorem: http://en.wikipedia.org/wiki/CAP_theorem

Recovery from a Single Node Failure
===================================

If one of the nodes in the cluster fails, the other nodes will
continue to operate as usual. When the failed node comes up again,
it automatically synchronizes with the other nodes before it is
allowed back into the cluster. No data is lost when a node fails.

See chapter
:ref:`Node Provisioning and Recovery <Node Provisioning and Recovery>`
for more information on manual node recover.

---------------
 Split-brain
---------------
.. _`Split-brain`:

A split-brain situation is a cluster failure where database nodes
in the cluster begin to operate autonomously from each other.
Data can get irreparably corrupted as two different database nodes
update the data independently.

Like any quorum-based system, *Galera Cluster* is subject to the
split-brain condition when the quorum algorithm fails to select a
primary component. This can happen, for example, in a cluster without
a backup switch if the main switch fails. However, the most likely
split-brain situation is when a single node fails in a two-node cluster.
Thus, it is strongly advised that the minimum *Galera Cluster*
configuration is three nodes.

*Galera Cluster* is designed to avoid split-brain
situations. If a cluster is split into two partitions of equal size,
both of the split partitions end up in a non-primary component
(unless explicitly configured otherwise). In this situation, proceed
as described in chapter :ref:`Node Resetting the Quorum <Resetting the Quorum>`.

------------------------
 State Transfer Failure
------------------------
.. _`State Transfer Failure`:

A failure in state transfer renders the receiving node unusable.
If a state transfer failure is detected, the receiving node will
abort.

Restarting the node after a *mysqldump* failure may require manual
restoring of the administrative tables. The rsync method does not
have this issue, since it does not need the server to be in
operational state.
