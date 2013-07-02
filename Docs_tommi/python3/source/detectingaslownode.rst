=========================
 Detecting a Slow Node
=========================
.. _`Detecting a Slow Node`:

By design, the performance of the cluster cannot be higher than
the performance of the slowest node on the cluster. Even if you
have one node only, its performance can be considerably lower
when compared with running the same server in a standalone mode
(without a *wsrep provider*). This is particularly true for big
transactions even if they were within the transaction size limits.
This is why it is important to be able to detect a slow node
on the cluster.

The slowest cluster node will have the highest values for the
following variables::

    wsrep_flow_control_sent

and::

    wsrep_local_recv_queue_avg

The lower the values are the better.