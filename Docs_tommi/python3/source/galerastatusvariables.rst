=========================
 Galera Status Variables
=========================
.. _`MySQL wsrep Options`:

These variables are Galera 0.8.x status variables. There are
two types of wsrep-related status variables:

- Galera-specific variables exported by Galera
- Variables exported by MySQL. These variables
  are for the general wsrep provider. 

This distinction is of importance for developers only.
For convenience, all status variables are presented as
a single list below.

The location (L) of the variable is presented in the second
column from the left. The values are:

- G |---| Galera
- M |---| MySQL

+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| Status Variable                       | L | Example Value              | Description                                                    |
+=======================================+===+============================+================================================================+
| ``wsrep_local_state_uuid``            | G | *e2c9a15e-5485-11e0-0800-* | The UUID of the state stored on this node. See                 |
|                                       |   | *6bbb637e7211*             | :ref:`wsrep API <wsrep API>`.                                  |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_last_committed``              | G | *409745*                   | Sequence number of the last committed transaction. See         |
|                                       |   |                            | :ref:`wsrep API <wsrep API>`.                                  |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_replicated``                  | G | *16109*                    | Total number of writesets replicated (sent to other nodes).    |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_replicated_bytes``            | G | *6526788*                  | Total size of writesets replicated.                            |
|                                       |   |                            |                                                                |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_received``                    | G | *17831*                    | Total number of writesets received from other nodes.           |
|                                       |   |                            |                                                                |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_received_bytes``              | G | *6637093*                  | Total size of writesets received from other nodes.             |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_local_commits``               | G | *14981*                    | Total number of local transactions committed.                  |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_local_cert_failures``         | G | *333*                      | Total number of local transactions that failed certification   |
|                                       |   |                            | test.                                                          |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_local_bf_aborts``             | G | *960*                      | Total number of local transactions that were aborted by slave  |
|                                       |   |                            | transactions while in execution.                               |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_local_replays``               | G | *0*                        | Total number of transaction replays due to *asymmetric lock    |
|                                       |   |                            | granularity*.                                                  |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_local_send_queue``            | G | *1*                        | Current (instantaneous) length of the send queue.              |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_local_send_queue_avg``        | G | *0.145000*                 | Send queue length averaged over interval since the last status |
|                                       |   |                            | query. Values considerably larger than 0.0 indicate            |
|                                       |   |                            | replication throttling or network throughput issue.            |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_local_recv_queue``            | G | *0*                        | Current (instantaneous) length of the recv queue.              |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_local_recv_queue_avg``        | G | *3.348452*                 | Recv queue length averaged over interval since the last status |
|                                       |   |                            | query. Values considerably larger than 0.0 mean that the node  |
|                                       |   |                            | cannot apply writesets as fast as they are received and will   |
|                                       |   |                            | generate a lot of replication throttling.                      |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_flow_control_paused``         | G | *0.184353*                 | The fraction of time since the last status query that          |
|                                       |   |                            | replication was paused due to flow control.                    |
|                                       |   |                            |                                                                |
|                                       |   |                            | In other words, how much the slave lag is slowing down         |
|                                       |   |                            | the cluster.                                                   |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_flow_control_sent``           | G | *7*                        | Number of ``FC_PAUSE`` events sent since the last status       |
|                                       |   |                            | query.                                                         |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_flow_control_recv``           | G | *11*                       | Number of ``FC_PAUSE`` events received since the last status   |
|                                       |   |                            | query (counts the events sent).                                |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_cert_deps_distance``          | G | *23.88889*                 | Average distance between highest and lowest seqno that can be  |
|                                       |   |                            | possibly applied in parallel (potential degree of              |
|                                       |   |                            | parallelization).                                              |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_apply_oooe``                  | G | *0.671120*                 | How often applier started writeset applying out-of-order       |
|                                       |   |                            | (parallelization efficiency).                                  |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_apply_oool``                  | G | *0.195248*                 | How often writeset was so slow to apply that writeset with     |
|                                       |   |                            | higher seqno's were applied earlier. Values closer to 0 refer  |
|                                       |   |                            | to a greater gap between slow and fast writesets.              |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_apply_window``                | G | *5.163966*                 | Average distance between highest and lowest concurrently       |
|                                       |   |                            | applied seqno.                                                 |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_commit_oooe``                 | G | *0.000000*                 | How often a transaction was committed out of order.            |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_commit_oool``                 | G | *0.000000*                 | No meaning.                                                    |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_commit_window``               | G | *0.000000*                 | Average distance between highest and lowest concurrently       |
|                                       |   |                            | committed seqno.                                               |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_local_state``                 | G | *4*                        | Internal Galera FSM state number. See                          |
|                                       |   |                            | :ref:`Node State Changes <Node State Changes>`.                |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_local_state_comment``         | G | *Synced*                   | Human-readable explanation of the state.                       |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_incoming_addresses``          | G | *10.0.0.1:3306,10.0.0.2:*  | Comma-separated list of incoming server addresses              |
|                                       |   | *3306,undefined*           | in the cluster component.                                      |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_cluster_conf_id``             | M | *34*                       | Total number of cluster membership changes happened.           |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_cluster_size``                | M | *3*                        | Current number of members in the cluster.                      |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_cluster_state_uuid``          | M | *e2c9a15e-5485-11e0-*      | See :ref:`wsrep API <wsrep API>`.                              |
|                                       |   | *0800-6bbb637e7211*        |                                                                |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_cluster_status``              | M | *Primary*                  | Status of this cluster component: *PRIMARY*/*NON_PRIMARY*.     |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_local_index``                 | M | *1*                        | This node index in the cluster (base 0).                       |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_ready``                       | M | *ON*                       | Whether the server is ready to accept queries. If this status  |
|                                       |   |                            | is *OFF*, almost all of the queries fill fail with::           |
|                                       |   |                            |                                                                |
|                                       |   |                            |   ERROR 1047 (08S01) Unknown Command                           |
|                                       |   |                            |                                                                |
|                                       |   |                            | unless the ``wsrep_on`` session variable is set to *0*.        |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_cert_index_size``             | G | *30936*                    | The number of entries in the certification index.              |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| `` wsrep_protocol_version``           | G | *4*                        | The version of the wsrep protocol used.                        |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+
| ``wsrep_connected``                   | G | *ON*                       | If the value is *OFF*, the node has not yet connected to any   |
|                                       |   |                            | of the cluster components. This may be due to                  |
|                                       |   |                            | misconfiguration. Check the error log for proper diagnostics.  |
+---------------------------------------+---+----------------------------+----------------------------------------------------------------+




.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
