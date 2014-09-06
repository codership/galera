=========================
 Galera Status Variables
=========================
.. _`MySQL wsrep Options`:

These variables are *Galera Cluster* 0.8.x status variables. There are two types of wsrep-related status variables:

- Galera Cluster-specific variables exported by *Galera Cluster*

- Variables exported by MySQL. These variables are for the general wsrep provider. 

This distinction is of importance for developers only.  For convenience, all status variables are presented as a single list below.

The location (L) of the variable is presented in the second column from the left. The values are:

- *G* for Galera Cluster
- *M* for MySQL

+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| Status Variable                       | L | Example Value              | Introduced           | Deprecated                              |
+=======================================+===+============================+======================+=========================================+
| :ref:`wsrep_evs_repl_latency          | G |                            | 3.0                  |                                         |
| <wsrep_evs_repl_latency>`             |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_state_uuid          | G | *e2c9a15e-5485-11e0-0800-* |                      |                                         |
| <wsrep_local_state_uuid>`             |   | *6bbb637e7211*             |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_last_committed            | G | *409745*                   |                      |                                         |
| <wsrep_last_committed>`               |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_replicated                | G | *16109*                    |                      |                                         |
| <wsrep_replicated>`                   |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_replicated_bytes          | G | *6526788*                  |                      |                                         |
| <wsrep_replicated_bytes>`             |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_repl_keys                 | G | *797399*                   |                      |                                         |
| <wsrep_repl_keys>`                    |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_repl_keys_bytes           | G | *11203721*                 |                      |                                         |
| <wsrep_repl_keys_bytes>`              |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_repl_data_bytes           | G | *265035226*                |                      |                                         |
| <wsrep_repl_data_bytes>`              |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_repl_other_bytes          | G | *0*                        |                      |                                         |
| <wsrep_repl_other_bytes>`             |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_received                  | G | *17831*                    |                      |                                         |
| <wsrep_received>`                     |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_received_bytes            | G | *6637093*                  |                      |                                         |
| <wsrep_received_bytes>`               |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_commits             | G | *14981*                    |                      |                                         |
| <wsrep_local_commits>`                |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_cert_failures       | G | *333*                      |                      |                                         |
| <wsrep_local_cert_failures>`          |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_bf_aborts           | G | *960*                      |                      |                                         |
| <wsrep_local_bf_aborts>`              |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_replays             | G | *0*                        |                      |                                         |
| <wsrep_local_replays>`                |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_send_queue          | G | *1*                        |                      |                                         |
| <wsrep_local_send_queue>`             |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_send_queue_max      | G | *10*                       |                      |                                         |
| <wsrep_local_send_queue_max>`         |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_send_queue_min      | G | *0*                        |                      |                                         |
| <wsrep_local_send_queue_min>`         |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_send_queue_avg      | G | *0.145000*                 |                      |                                         |
| <wsrep_local_send_queue_avg>`         |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_recv_queue          | G | *0*                        |                      |                                         |
| <wsrep_local_recv_queue>`             |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_recv_queue_max      | G | *10*                       |                      |                                         |
| <wsrep_local_recv_queue_max>`         |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_recv_queue_min      | G | *0*                        |                      |                                         |
| <wsrep_local_recv_queue_min>`         |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_recv_queue_avg      | G | *3.348452*                 |                      |                                         |
| <wsrep_local_recv_queue_avg>`         |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_cached_downto       | G | *18446744073709551615*     |                      |                                         |
| <wsrep_local_cached_downto>`          |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_flow_control_paused_ns    | G | *20222491180*              |                      |                                         |
| <wsrep_flow_control_paused_ns>`       |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_flow_control_paused       | G | *0.184353*                 |                      |                                         |
| <wsrep_flow_control_paused>`          |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_flow_control_sent         | G | *7*                        |                      |                                         |
| <wsrep_flow_control_sent>`            |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_flow_control_recv         | G | *11*                       |                      |                                         |
| <wsrep_flow_control_recv>`            |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_cert_deps_distance        | G | *23.88889*                 |                      |                                         |
| <wsrep_cert_deps_distance>`           |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_apply_oooe                | G | *0.671120*                 |                      |                                         |
| <wsrep_apply_oooe>`                   |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_apply_oool                | G | *0.195248*                 |                      |                                         |
| <wsrep_apply_oool>`                   |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_apply_window              | G | *5.163966*                 |                      |                                         |
| <wsrep_apply_window>`                 |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_commit_oooe               | G | *0.000000*                 |                      |                                         |
| <wsrep_commit_oooe>`                  |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_commit_oool               | G | *0.000000*                 |                      |                                         |
| <wsrep_commit_oool>`                  |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_commit_window             | G | *0.000000*                 |                      |                                         |
| <wsrep_commit_window>`                |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_state               | G | *4*                        |                      |                                         |
| <wsrep_local_state>`                  |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_state_comment       | G | *Synced*                   |                      |                                         |
| <wsrep_local_state_comment>`          |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_incoming_addresses        | G | *10.0.0.1:3306,10.0.0.2:*  |                      |                                         |
| <wsrep_incoming_addresses>`           |   | *3306,undefined*           |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_cluster_conf_id           | M | *34*                       |                      |                                         |
| <wsrep_cluster_conf_id>`              |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_cluster_size              | M | *3*                        |                      |                                         |
| <wsrep_cluster_size>`                 |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_cluster_state_uuid        | M | *e2c9a15e-5485-11e0-*      |                      |                                         |
| <wsrep_cluster_state_uuid>`           |   | *0800-6bbb637e7211*        |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_cluster_status            | M | *Primary*                  |                      |                                         |
| <wsrep_cluster_status>`               |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_local_index               | M | *1*                        |                      |                                         |
| <wsrep_local_index>`                  |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_provider_name             | M | *Galera*                   |                      |                                         |
| <wsrep_provider_name>`                |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_provider_vendor           | M | *Codership Oy*             |                      |                                         |
| <wsrep_provider_vendor>`              |   | *<info@codership.com>*     |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_provider_version          | M | *25.3.5-wheezy(rXXXX)*     |                      |                                         |
| <wsrep_provider_version>`             |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_ready                     | M | *ON*                       |                      |                                         |
| <wsrep_ready>`                        |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_cert_index_size           | G | *30936*                    |                      |                                         |
| <wsrep_cert_index_size>`              |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_protocol_version          | G | *4*                        |                      |                                         |
| <wsrep_protocol_version>`             |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+
| :ref:`wsrep_connected                 | G | *ON*                       |                      |                                         |
| <wsrep_connected>`                    |   |                            |                      |                                         |
+---------------------------------------+---+----------------------------+----------------------+-----------------------------------------+


.. rubric:: ``wsrep_evs_repl_latency``
.. _`wsrep_evs_repl_latency`:
.. index::
   pair: Parameters; wsrep_evs_repl_latency

This status variable provides figures for the replication latency on group communication.  It measures latency from the time point when a message is sent out to the time point when a message is received.  As replication is a group operation, this essentially gives you the slowest ACK and longest RTT in the cluster.

For example,

.. code-block:: mysql

	SHOW STATUS LIKE 'wsrep_evs_repl_latency';

	+------------------------+------------------------------------------+
	| Variable_name          | Value                                    |
	+------------------------+------------------------------------------+
	| wsrep_evs_repl_latency | 0.00243433/0.144022/0.591963/0.215824/13 |
	+------------------------+------------------------------------------+

The units are in seconds.  The format of the return value is:

.. code-block:: plain

	Minimum / Average / Maximum / Standard Deviation / Sample Size

This variable periodically resets.  You can control the reset interval using the :ref:`evs.stats_report_period <evs.stats_report_period>` parameter.  The default value is 1 minute.




.. rubric:: ``wsrep_local_state_uuid``
.. _`wsrep_local_state_uuid`:
.. index::
   pair: Parameters; wsrep_local_state_uuid

The UUID of the state stored on this node. See :ref:`wsrep API <wsrep API>`. 


.. rubric:: ``wsrep_last_committed``
.. _`wsrep_last_committed`:
.. index::
   pair: Parameters; wsrep_last_committed

Sequence number of the last committed transaction. See :ref:`wsrep API <wsrep API>`.  


.. rubric:: ``wsrep_replicated``
.. _`wsrep_replicated`:
.. index::
   pair: Parameters; wsrep_replicated

Total number of write-sets replicated (sent to other nodes).


.. rubric:: ``wsrep_replicated_bytes``
.. _`wsrep_replicated_bytes`:
.. index::
   pair: Parameters; wsrep_replicated_bytes

Total size of write-sets replicated.


.. rubric:: ``wsrep_repl_keys``
.. _`wsrep_repl_keys`:
.. index::
   pair: Parameters; wsrep_repl_keys

Total number of keys replicated.


.. rubric:: ``wsrep_repl_keys_bytes``
.. _`wsrep_repl_keys_bytes`:
.. index::
   pair: Parameters; wsrep_repl_keys_bytes

Total size of keys replicated.


.. rubric:: ``wsrep_repl_data_bytes``
.. _`wsrep_repl_data_bytes`:
.. index::
   pair: Parameters; wsrep_repl_data_bytes

Total size of data replicated.


.. rubric:: ``wsrep_repl_other_bytes``
.. _`wsrep_repl_other_bytes`:
.. index::
   pair: Parameters; wsrep_repl_other_bytes

Total size of other bits replicated.


.. rubric:: ``wsrep_received``
.. _`wsrep_received`:
.. index::
   pair: Parameters; wsrep_received

Total number of write-sets received from other nodes.


.. rubric:: ``wsrep_received_bytes``
.. _`wsrep_received_bytes`:
.. index::
   pair: Parameters; wsrep_received_bytes

Total size of write-sets received from other nodes.


.. rubric:: ``wsrep_local_commits``
.. _`wsrep_local_commits`:
.. index::
   pair: Parameters; wsrep_local_commits

Total number of local transactions committed.


.. rubric:: ``wsrep_local_cert_failures``
.. _`wsrep_local_cert_failures`:
.. index::
   pair: Parameters; wsrep_local_cert_failures

Total number of local transactions that failed certification test.

.. rubric:: ``wsrep_local_bf_aborts``
.. _`wsrep_local_bf_aborts`:
.. index::
   pair: Parameters; wsrep_local_bf_aborts

Total number of local transactions that were aborted by slave transactions while in execution.

.. rubric:: ``wsrep_local_replays``
.. _`wsrep_local_replays`:
.. index::
   pair: Parameters; wsrep_local_replays

Total number of transaction replays due to *asymmetric lock granularity*.


.. rubric:: ``wsrep_local_send_queue``
.. _`wsrep_local_send_queue`:
.. index::
   pair: Parameters; wsrep_local_send_queue

Current (instantaneous) length of the send queue.

.. rubric:: ``wsrep_local_send_queue_max``

.. _`wsrep_local_send_queue_max`:

.. index::
   pair: Parameters; wsrep_local_send_queue_max

The maximum length of the send queue since the last status query. 


.. rubric:: ``wsrep_local_send_queue_min``

.. _`wsrep_local_send_queue_min`:

.. index::
   pair: Parameters; wsrep_local_send_queue_min

The minimum length of the send queue since the last status query. 


.. rubric:: ``wsrep_local_send_queue_avg``
.. _`wsrep_local_send_queue_avg`:
.. index::
   pair: Parameters; wsrep_local_send_queue_avg

Send queue length averaged over interval since the last status query. Values considerably larger than 0.0 indicate replication throttling or network throughput issue. 


.. rubric:: ``wsrep_local_recv_queue``
.. _`wsrep_local_recv_queue`:
.. index::
   pair: Parameters; wsrep_local_recv_queue

Current (instantaneous) length of the recv queue. 


.. rubric:: ``wsrep_local_recv_queue_max``

.. _`wsrep_local_recv_queue_max`:

.. index::
   pair: Parameters; wsrep_local_recv_queue_max

The maximum length of the recv queue since the last status query. 


.. rubric:: ``wsrep_local_recv_queue_min``

.. _`wsrep_local_recv_queue_min`:

.. index::
   pair: Parameters; wsrep_local_recv_queue_min

The minimum length of the recv queue since the last status query. 


.. rubric:: ``wsrep_local_recv_queue_avg``
.. _`wsrep_local_recv_queue_avg`:
.. index::
   pair: Parameters; wsrep_local_recv_queue_avg

Recv queue length averaged over interval since the last status query. Values considerably larger than 0.0 mean that the node cannot apply writesets as fast as they are received and will generate a lot of replication throttling. 


.. rubric:: ``wsrep_local_cached_downto``
.. _`wsrep_local_cached_downto`:
.. index::
   pair: Parameters; wsrep_local_cached_downto

The lowest sequence number in ``gcache``.


.. rubric:: ``wsrep_flow_control_paused_ns``
.. _`wsrep_flow_control_paused_ns`:
.. index::
   pair: Parameters; wsrep_flow_control_paused_ns

The total time spent in a paused state measured in nanoseconds.


.. rubric:: ``wsrep_flow_control_paused``
.. _`wsrep_flow_control_paused`:
.. index::
   pair: Parameters; wsrep_flow_control_paused

The fraction of time since the last status query that replication was paused due to flow control.

In other words, how much the slave lag is slowing down the cluster. 


.. rubric:: ``wsrep_flow_control_sent``
.. _`wsrep_flow_control_sent`:
.. index::
   pair: Parameters; wsrep_flow_control_sent

Number of ``FC_PAUSE`` events sent since the last status query. 


.. rubric:: ``wsrep_flow_control_recv``
.. _`wsrep_flow_control_recv`:
.. index::
   pair: Parameters; wsrep_flow_control_recv

Number of ``FC_PAUSE`` events received since the last status query (counts the events sent). 


.. rubric:: ``wsrep_cert_deps_distance``
.. _`wsrep_cert_deps_distance`:
.. index::
   pair: Parameters; wsrep_cert_deps_distance

Average distance between highest and lowest ``seqno`` value that can be possibly applied in parallel (potential degree of parallelization). 


.. rubric:: ``wsrep_apply_oooe``
.. _`wsrep_apply_oooe`:
.. index::
   pair: Parameters; wsrep_apply_oooe

How often applier started writeset applying out-of-order (parallelization efficiency).


.. rubric:: ``wsrep_apply_oool``
.. _`wsrep_apply_oool`:
.. index::
   pair: Parameters; wsrep_apply_oool

How often writeset was so slow to apply that write-set with higher seqno's were applied earlier. Values closer to 0 refer to a greater gap between slow and fast write-sets.


.. rubric:: ``wsrep_apply_window``
.. _`wsrep_apply_window`:
.. index::
   pair: Parameters; wsrep_apply_window

Average distance between highest and lowest concurrently applied seqno. 


.. rubric:: ``wsrep_commit_oooe``
.. _`wsrep_commit_oooe`:
.. index::
   pair: Parameters; wsrep_commit_oooe

How often a transaction was committed out of order.


.. rubric:: ``wsrep_commit_oool``
.. _`wsrep_commit_oool`:
.. index::
   pair: Parameters; wsrep_commit_oool

No meaning.


.. rubric:: ``wsrep_commit_window``
.. _`wsrep_commit_window`:
.. index::
   pair: Parameters; wsrep_commit_window

Average distance between highest and lowest concurrently committed seqno. 


.. rubric:: ``wsrep_local_state``
.. _`wsrep_local_state`:
.. index::
   pair: Parameters; wsrep_local_state

Internal Galera Cluster FSM state number. See :ref:`Node State Changes <Node State Changes>`. 


.. rubric:: ``wsrep_local_state_comment``
.. _`wsrep_local_state_comment`:
.. index::
   pair: Parameters; wsrep_local_state_comment

Human-readable explanation of the state.


.. rubric:: ``wsrep_incoming_addresses``
.. _`wsrep_incoming_addresses`:
.. index::
   pair: Parameters; wsrep_incoming_addresses

Comma-separated list of incoming server addresses in the cluster component.


.. rubric:: ``wsrep_cluster_conf_id``
.. _`wsrep_cluster_conf_id`:
.. index::
   pair: Parameters; wsrep_cluster_conf_id

Total number of cluster membership changes happened. 


.. rubric:: ``wsrep_cluster_size``
.. _`wsrep_cluster_size`:
.. index::
   pair: Parameters; wsrep_cluster_size

Current number of members in the cluster.


.. rubric:: ``wsrep_cluster_state_uuid``
.. _`wsrep_cluster_state_uuid`:
.. index::
   pair: Parameters; wsrep_cluster_state_uuid

See :ref:`wsrep API <wsrep API>`.


.. rubric:: ``wsrep_cluster_status``
.. _`wsrep_cluster_status`:
.. index::
   pair: Parameters; wsrep_cluster_status

Status of this cluster component: *PRIMARY* or *NON_PRIMARY*.


.. rubric:: ``wsrep_local_index``
.. _`wsrep_local_index`:
.. index::
   pair: Parameters; wsrep_local_index

This node index in the cluster (base 0).


.. rubric:: ``wsrep_provider_name``
.. _`wsrep_provider_name`:
.. index::
   pair: Parameters; wsrep_provider_name

The name of the wsrep provider.


.. rubric:: ``wsrep_provider_vendor``
.. _`wsrep_provider_vendor`:
.. index::
   pair: Parameters; wsrep_provider_vendor

The name of the wsrep provider vendor.


.. rubric:: ``wsrep_provider_version``
.. _`wsrep_provider_version`:
.. index::
   pair: Parameters; wsrep_provider_version

The name of the wsrep provider version string.


.. rubric:: ``wsrep_ready``
.. _`wsrep_ready`:
.. index::
   pair: Parameters; wsrep_ready

Whether the server is ready to accept queries. If this status is ``OFF``, almost all of the queries fill fail with::

    ERROR 1047 (08S01) Unknown Command

unless the ``wsrep_on`` session variable is set to ``0``.


.. rubric:: ``wsrep_cert_index_size``
.. _`wsrep_cert_index_size`:
.. index::
   pair: Parameters; wsrep_cert_index_size

The number of entries in the certification index.


.. rubric:: ``wsrep_protocol_version``
.. _`wsrep_protocol_version`:
.. index::
   pair: Parameters; wsrep_protocol_version

The version of the wsrep protocol used.


.. rubric:: ``wsrep_connected``
.. _`wsrep_connected`:
.. index::
   pair: Parameters; wsrep_connected

If the value is ``OFF``, the node has not yet connected to any of the cluster components. This may be due to misconfiguration. Check the error log for proper diagnostics.


.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
