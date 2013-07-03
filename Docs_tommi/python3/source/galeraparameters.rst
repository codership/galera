==================
 Galera Parameters
==================
.. _`Galera Parameters`:

As of version 0.8, *Galera Cluster*
accepts parameters as semicolon-separated
key value pair lists, such as ``key1 = value1; key2 = value2``.
In this way, you can configure an arbitrary number of
*Galera Cluster* parameters
in one call. A key consists of parameter group and parameter name::

  <group>.<name>

Where ``<group>`` roughly corresponds to some Galera module.

Table legend:

- *Numeric values* |---| *Galera Cluster*
  understands the following numeric modifiers:
  K, M, G, T standing for |210|, |220|, |230| and |240| respectively.
- *Boolean values* |---| *Galera Cluster*
  accepts the following boolean values: 0, 1, yes, no, true, false, on, off.
- Time periods must be expressed in the ISO8601 format. See also the examples below.

.. |210| replace:: 2\ :sup:`10`\
.. |220| replace:: 2\ :sup:`20`\
.. |230| replace:: 2\ :sup:`30`\
.. |240| replace:: 2\ :sup:`40`\

+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| Parameter                             | Default               |  Introduced           | Deprecated         | Dynamic  |
+=======================================+=======================+=======================+====================+==========+
| :ref:`protonet.backend                | *asio*                | 1.0                   | n/a                | No       |
| <protonet.backend>`                   |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`protonet.version                | *0*                   | 1.0                   | n/a                | No       |
| <protonet.version>`                   |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`socket.ssl_cert                 |                       | 1.0                   | n/a                | No       |
| <socket.ssl_cert>`                    |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`socket.ssl_key                  |                       | 1.0                   | n/a                | No       |
| <socket.ssl_key>`                     |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`socket.ssl_compression          | *yes*                 | 1.0                   | n/a                | No       |
| <socket.ssl_compression>`             |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`socket.ssl_cipher               | *AES128-SHA*          | 1.0                   | n/a                | No       |
| <socket.ssl_cipher>`                  |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gmcast.listen_addr              | *tcp://0.0.0.0:4567*  | 1.0                   | n/a                | No       |
| <gmcast.listen_addr>`                 |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gmcast.mcast_addr               |                       | 1.0                   | n/a                | No       |
| <gmcast.mcast_addr>`                  |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gmcast.mcast_ttl                | *1*                   | 1.0                   | n/a                | No       |
| <gmcast.mcast_ttl>`                   |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gmcast.peer_timeout             | *PT3S*                | 1.0                   | n/a                | No       |
| <gmcast.peer_timeout>`                |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gmcast.time_wait                | *PT5S*                | 1.0                   | n/a                | No       |
| <gmcast.time_wait>`                   |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gmcast.version                  | *0*                   | 1.0                   | n/a                | No       |
| <gmcast.version>`                     |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.causal_keepalive_period     |                       | 1.0                   | n/a                | No       |
| <evs.causal_keepalive_period>`        |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.consensus_timeout           | *PT30S*               | 1.0                   | Yes, as of 2.0     | No       |
| <evs.consensus_timeout>`              |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.debug_log_mask              | *0x1*                 | 1.0                   | n/a                | Yes      |
| <evs.debug_log_mask>`                 |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.inactive_check_period       | *PT1S*                | 1.0                   | n/a                | No       |
| <evs.inactive_check_period>`          |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.inactive_timeout            | *PT15S*               | 1.0                   | n/a                | No       |
| <evs.inactive_timeout>`               |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.info_log_mask               | *0*                   | 1.0                   | n/a                | No       |
| <evs.info_log_mask>`                  |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.install_timeout             | *PT15S*               | 1.0                   | n/a                | Yes      |
| <evs.install_timeout>`                |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.join_retrans_period         | *PT1S*                | 1.0                   | n/a                | Yes      |
| <evs.join_retrans_period>`            |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.keepalive_period            | *PT1S*                | 1.0                   | n/a                | No       |
| <evs.keepalive_period>`               |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.max_install_timeouts        | *1*                   | 1.0                   | n/a                | No       |
| <evs.max_install_timeouts>`           |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.send_window                 | *4*                   | 1.0                   | n/a                | Yes      |
| <evs.send_window>`                    |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.stats_report_period         | *PT1M*                | 1.0                   | n/a                | No       |
| <evs.stats_report_period>`            |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.suspect_timeout             | *PT5S*                | 1.0                   | n/a                | No       |
| <evs.suspect_timeout>`                |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.use_aggregate               | *true*                | 1.0                   | n/a                | No       |
| <evs.use_aggregate>`                  |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.user_send_window            | *2*                   | 1.0                   | n/a                | Yes      |
| <evs.user_send_window>`               |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.view_forget_timeout         | *PT5M*                | 1.0                   | n/a                | No       |
| <evs.view_forget_timeout>`            |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`evs.version                     | *0*                   | 1.0                   | n/a                | No       |
| <evs.version>`                        |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`pc.bootstrap                    |                       | 1.0                   | n/a                | No       |
| <pc.bootstrap>`                       |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`pc.checksum                     | *true*                | 1.0                   | n/a                | No       |
| <pc.checksum>`                        |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`pc.ignore_sb                    | *false*               | 1.0                   | n/a                | Yes      | 
| <pc.ignore_sb>`                       |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`pc.ignore_quorum                | *false*               | 1.0                   | n/a                | Yes      |
| <pc.ignore_quorum>`                   |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`pc.linger                       | *PT2S*                | 1.0                   | n/a                | No       |
| <pc.linger>`                          |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`pc.npvo                         | *false*               | 1.0                   | n/a                | No       |
| <pc.npvo>`                            |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`pc.wait_prim                    | ``false``             | 1.0                   | n/a                | No       |
| <pc.wait_prim>`                       |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`pc.weight                       | *1*                   | 2.4                   | n/a                | Yes      |
| <pc.weight>`                          |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`pc.version                      | *0*                   | 1.0                   | n/a                | No       |
| <pc.version>`                         |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcs.fc_debug                    | *0*                   | 1.0                   | n/a                | No       |
| <gcs.fc_debug>`                       |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcs.fc_factor                   | *0.5*                 | 1.0                   | n/a                | Yes      |
| <gcs.fc_factor>`                      |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcs.fc_limit                    | *16*                  | 1.0                   | n/a                | Yes      |
| <gcs.fc_limit>`                       |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcs.fc_master_slave             | *NO*                  | 1.0                   | n/a                | No       |
| <gcs.fc_master_slave>`                |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcs.max_packet_size             | *32616*               | 1.0                   | n/a                | No       |
| <gcs.max_packet_size>`                |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcs.max_throttle                | *0.25*                | 1.0                   | n/a                | No       |
| <gcs.max_throttle>`                   |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcs.recv_q_hard_limit           | *LLONG_MAX*           | 1.0                   | n/a                | No       |
| <gcs.recv_q_hard_limit>`              |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcs.recv_q_soft_limit           | *0.25*                | 1.0                   | n/a                | No       |
| <gcs.recv_q_soft_limit>`              |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcs.sync_donor                  | *NO*                  | 1.0                   | n/a                | No       |
| <gcs.sync_donor>`                     |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`ist.recv_addr                   |                       | 1.0                   | n/a                | No       |
| <ist.recv_addr>`                      |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`replicator.commit_order         | *3*                   | 1.0                   | n/a                | No       |
| <replicator.commit_order>`            |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`replicator.causal_read_timeout  | *PT30S*               | 1.0                   | n/a                | No       |
| <replicator.causal_read_timeout>`     |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcache.dir                      |                       | 1.0                   | n/a                | No       |
| <gcache.dir>`                         |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcache.name                     | *"galera.cache"*      | 1.0                   | n/a                | No       |
| <gcache.name>`                        |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcache.size                     | *128Mb*               | 1.0                   | n/a                | No       |
| <gcache.size>`                        |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcache.page_size                | *128Mb*               | 1.0                   | n/a                | No       |
| <gcache.page_size>`                   |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcache.keep_pages_size          | *0*                   | 1.0                   | n/a                | No       |
| <gcache.keep_pages_size>`             |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+
| :ref:`gcache.mem_size                 | *0*                   | 1.0                   | n/a                | No       |
| <gcache.mem_size>`                    |                       |                       |                    |          |
+---------------------------------------+-----------------------+-----------------------+--------------------+----------+

.. rubric:: protonet.backend

.. _`protonet.backend`:

.. index::
   pair: Parameters; protonet.backend

Which transport backend to use. Currently only ASIO is supported.

.. rubric:: protonet.version

.. _`protonet.version`:

.. index::
   pair: Parameters; protonet.version

This status variable is used to check which transport
backend protocol  version is used. This variable is
mostly used for troubleshooting purposes.

.. rubric:: socket.ssl_cert

.. _`socket.ssl_cert`:

.. index::
   pair: Parameters; socket.ssl_cert

A path (absolute or relative to the working
directory )to an SSL certificate (in PEM format). 

.. rubric:: socket.ssl_key

.. _`socket.ssl_key`:

.. index::
   pair: Parameters; socket.ssl_key

A path (absolute or relative to the working
directory to a private key for a certificate
(in PEM format).

.. rubric:: socket.ssl_compression

.. _`socket.ssl_compression`:

.. index::
   pair: Parameters; socket.ssl_compression

Whether to enable compression on SSL connections.


.. rubric:: socket.ssl_cipher

.. _`socket.ssl_cipher`:

.. index::
   pair: Parameters; socket.ssl_cipher

Symmetric cipher to use. AES128 is used by default
it is considerably faster and no less secure than
AES256.

.. rubric:: gmcast.listen_addr

.. _`gmcast.listen_addr`:

.. index::
   pair: Parameters; gmcast.listen_addr

Address at which *Galera Cluster* listens to connections
from other nodes. By default the port to listen at
is taken from the connection address. This setting
can be used to overwrite that.

.. rubric:: gmcast.mcast_addr

.. _`gmcast.mcast_addr`:

.. index::
   pair: Parameters; gmcast.mcast_addr

If set, UDP multicast will be used for replication,
for example::

    gmcast.mcast_addr=239.192.0.11

The value must be the same on all nodes.

If you are planning to build a large cluster, we
recommend using UDP.


.. rubric:: gmcast.mcast_ttl

.. _`gmcast.mcast_ttl`:

.. index::
   pair: Parameters; gmcast.mcast_ttl

Time to live value for multicast packets.

.. rubric:: gmcast.peer_timeout

.. _`gmcast.peer_timeout`:

.. index::
   pair: Parameters; gmcast.peer_timeout

Connection timeout to initiate message relaying.


.. rubric:: gmcast.time_wait

.. _`gmcast.time_wait`:

.. index::
   pair: Parameters; gmcast.time_wait

Time to wait until allowing peer declared outside
of stable view to reconnect.

.. rubric:: gmcast.version

.. _`gmcast.version`:

.. index::
   pair: Parameters; gmcast.version

This status variable is used to check which gmcast
protocol version is used. This variable is mostly used
for troubleshooting purposes.


.. rubric:: evs.causal_keepalive_period

.. _`evs.causal_keepalive_period`:

.. index::
   pair: Parameters; evs.causal_keepalive_period

For developer use only. Defaults to ``evs.keepalive_period``.


.. rubric:: evs.consensus_timeout

.. _`evs.consensus_timeout`:

.. index::
   pair: Parameters; evs.consensus_timeout

Timeout on reaching the consensus about cluster
membership.

**Deprecated** See ``evs.install_timeout``.


.. rubric:: evs.debug_log_mask

.. _`evs.debug_log_mask`:

.. index::
   pair: Parameters; evs.debug_log_mask

Control EVS debug logging, only effective when
``wsrep_debug`` is in use.



.. rubric:: evs.inactive_check_period

.. _`evs.inactive_check_period`:

.. index::
   pair: Parameters; evs.inactive_check_period

How often to check for peer inactivity.


.. rubric:: evs.inactive_timeout

.. _`evs.inactive_timeout`:

.. index::
   pair: Parameters; evs.inactive_timeout

Hard limit on the inactivity period, after which
the node is pronounced dead.


.. rubric:: evs.info_log_mask

.. _`evs.info_log_mask`:

.. index::
   pair: Parameters; evs.info_log_mask

Control extra EVS info logging. Bits:
 
- *0x1* |---| extra view change info
- *0x2* |---| extra state change info
- *0x4* |---| statistics
- *0x8* |---| profiling (only in builds with profiling enabled)


.. rubric:: evs.install_timeout

.. _`evs.install_timeout`:

.. index::
   pair: Parameters; evs.install_timeout

Timeout on waiting for install message
acknowledgments. Successor to
``evs.consensus_timeout``.

.. rubric:: evs.join_retrans_period

.. _`evs.join_retrans_period`:

.. index::
   pair: Parameters; evs.join_retrans_period

How often to retransmit EVS join messages when forming the cluster membership.


.. rubric:: evs.keepalive_period

.. _`evs.keepalive_period`:

.. index::
   pair: Parameters; evs.keepalive_period

How often to emit keepalive beacons (in the absence of any other traffic).

.. rubric:: evs.max_install_timeouts

.. _`evs.max_install_timeouts`:

.. index::
   pair: Parameters; evs.max_install_timeouts

How many membership install rounds to try before
giving up (total rounds will be ``evs.max_install_timeouts`` + 2).


.. rubric:: evs.send_window

.. _`evs.send_window`:

.. index::
   pair: Parameters; evs.send_window

Maximum packets in replication at a time. For WAN
setups may be set considerably higher, e.g. 512.
Must be no less than ``evs.user_send_window``.
If you must use other that the default value, we
recommend using double the
``evs.user_send_window`` value.


.. rubric:: evs.stats_report_period

.. _`evs.stats_report_period`:

.. index::
   pair: Parameters; evs.stats_report_period

Control period of EVS statistics reporting.
the node is pronounced dead.

.. rubric:: evs.suspect_timeout

.. _`evs.suspect_timeout`:

.. index::
   pair: Parameters; evs.suspect_timeout

Inactivity period after which the node is
*suspected* to be dead. If all remaining nodes
agree on that, the node is dropped out of cluster
before ``evs.inactive_timeout`` is reached.


.. rubric:: evs.use_aggregate

.. _`evs.use_aggregate`:

.. index::
   pair: Parameters; evs.use_aggregate

Aggregate small packets into one, when possible.


.. rubric:: evs.user_send_window

.. _`evs.user_send_window`:

.. index::
   pair: Parameters; evs.user_send_window

Maximum data packets in replication at a time. 
For WAN setups, this value can be set considerably
higher, to, for example, 512.


.. rubric:: evs.view_forget_timeout

.. _`evs.view_forget_timeout`:

.. index::
   pair: Parameters; evs.view_forget_timeout

Drop past views from the view history after this timeout.

.. rubric:: evs.version

.. _`evs.version`:

.. index::
   pair: Parameters; evs.version

This status variable is used to check which evs
protocol version is used. This variable is mostly used
for troubleshooting purposes.

.. rubric:: pc.bootstrap

.. _`pc.bootstrap`:

.. index::
   pair: Parameters; pc.bootstrap

If you set this value to *true* is a signal to
turn a ``NON-PRIMARY`` component into ``PRIMARY``.


.. rubric:: pc.checksum

.. _`pc.checksum`:

.. index::
   pair: Parameters; pc.checksum

Checksum replicated messages.

.. rubric:: pc.ignore_sb

.. _`pc.ignore_sb`:

.. index::
   pair: Parameters; pc.ignore_sb

Should we allow nodes to process updates even in
the case of split brain? This is a dangerous
setting in multi-master setup, but should simplify
things in master-slave cluster (especially if only
2 nodes are used).

.. rubric:: pc.ignore_quorum

.. _`pc.ignore_quorum`:

.. index::
   pair: Parameters; pc.ignore_quorum

Completely ignore quorum calculations. For
example if the master splits from several slaves
it still remains operational. Use with extreme
caution even in master-slave setups, as slaves
will not automatically reconnect to master in this case.

.. rubric:: pc.linger

.. _`pc.linger`:

.. index::
   pair: Parameters; pc.linger

The period for which the PC protocol waits for the
EVS termination.

.. rubric:: pc.npvo

.. _`pc.npvo`:

.. index::
   pair: Parameters; pc.npvo

If set to ``true``, the more recent primary
component overrides older ones in the case of
conflicting primaries. 


.. rubric:: pc.wait_prim

.. _`pc.wait_prim`:

.. index::
   pair: Parameters; pc.wait_prim

If set to ``true``, the node waits for the primary
component forever. Useful to bring up a
non-primary component and make it primary with
``pc.bootstrap``.

.. rubric:: pc.weight

.. _`pc.weight`:

.. index::
   pair: Parameters; pc.weight

As of version 2.4. Node weight for quorum calculation.


.. rubric:: pc.version

.. _`pc.version`:

.. index::
   pair: Parameters; pc.version

This status variable is used to check which pc
protocol version is used. This variable is mostly used
for troubleshooting purposes.


.. rubric:: gcs.fc_debug

.. _`gcs.fc_debug`:

.. index::
   pair: Parameters; gcs.fc_debug

Post debug statistics about SST flow every this number of writesets. 

.. rubric:: gcs.fc_factor

.. _`gcs.fc_factor`:

.. index::
   pair: Parameters; gcs.fc_factor

Resume replication after recv queue drops below this fraction of ``gcs.fc_limit``.



.. rubric:: gcs.fc_limit

.. _`gcs.fc_limit`:

.. index::
   pair: Parameters; gcs.fc_limit

Pause replication if recv queue exceeds this
number of  writesets. For master-slave setups this
number can be increased considerably.


.. rubric:: gcs.fc_master_slave

.. _`gcs.fc_master_slave`:

.. index::
   pair: Parameters; gcs.fc_master_slave

Should we assume that there is only one master in the group?


.. rubric:: gcs.max_packet_size

.. _`gcs.max_packet_size`:

.. index::
   pair: Parameters; gcs.max_packet_size

All writesets exceeding that size will be fragmented.


.. rubric:: gcs.max_throttle

.. _`gcs.max_throttle`:

.. index::
   pair: Parameters; gcs.max_throttle

How much to throttle replication rate during state
transfer (to avoid running out of memory). Set the
value to 0.0 if stopping replication is acceptable
for completing state transfer. 


.. rubric:: gcs.recv_q_hard_limit

.. _`gcs.recv_q_hard_limit`:

.. index::
   pair: Parameters; gcs.recv_q_hard_limit

Maximum allowed size of recv queue. This should
normally be half of (RAM + swap). If this limit is
exceeded, *Galera Cluster* will abort the server.


.. rubric:: gcs.recv_q_soft_limit

.. _`gcs.recv_q_soft_limit`:

.. index::
   pair: Parameters; gcs.recv_q_soft_limit

The fraction of ``gcs.recv_q_hard_limit`` after
which replication rate will be throttled.

The degree of throttling is a linear function of
recv queue size and goes from 1.0 (*full rate*)
at ``gcs.recv_q_soft_limit`` to
``gcs.max_throttle`` at ``gcs.recv_q_hard_limit``
Note that *full rate*, as estimated between 0 and
``gcs.recv_q_soft_limit`` is a very imprecise
estimate of a regular replication rate. 

.. rubric:: gcs.sync_donor

.. _`gcs.sync_donor`:

.. index::
   pair: Parameters; gcs.sync_donor

Should the rest of the cluster keep in sync with
the donor? *Yes* means that if the donor is
blocked by state transfer, the whole cluster
is blocked with it.

If you choose to use value *YES*, it is
theoretically possible that the donor node cannot
keep up with the rest of the cluster due to the
extra load from the SST. If the node lags behind,
it may send flow control messages stalling the
whole cluster. However, you can monitor this using
the ``wsrep_flow_control_paused`` status variable.

.. rubric:: ist.recv_addr

.. _`ist.recv_addr`:

.. index::
   pair: Parameters; ist.recv_addr

As of 2.0. Address to listen for Incremental State
Transfer. By default this is the
``<address>:<port+1>`` from ``wsrep_node_address``.


.. rubric:: replicator.commit_order

.. _`replicator.commit_order`:

.. index::
   pair: Parameters; replicator.commit_order

Whether to allow Out-Of-Order committing (improves
parallel applying performance). Possible settings:

- 0 |---| BYPASS: all commit order monitoring is
  switched off (useful for measuring performance
  penalty)
- 1 |---| OOOC: allow out of order committing for
  all transactions
- 2 |---| LOCAL_OOOC: allow out of order
  committing only for local transactions
- 3 |---| NO_OOOC: no out of order committing is
  allowed (strict total order committing)

.. rubric:: replicator.causal_read_timeout

.. _`replicator.causal_read_timeout`:

.. index::
   pair: Parameters; replicator.causal_read_timeout

Sometimes causal reads need to timeout.

.. rubric:: gcache.dir

.. _`gcache.dir`:

.. index::
   pair: Parameters; gcache.dir

Directory where GCache should place its files.
Defaults to the working directory. 


.. rubric:: gcache.name

.. _`gcache.name`:

.. index::
   pair: Parameters; gcache.name


Name of the ring buffer storage file. 


.. rubric:: gcache.size

.. _`gcache.size`:

.. index::
   pair: Parameters; gcache.size


Size of the persistent on-disk ring buffer storage.
This will be preallocated on startup. 

The buffer file name is ``galera.cache``.

.. seealso:: Chapter :ref:`Customizing GCache Size <Customizing GCache Size>`.  


.. rubric:: gcache.page_size

.. _`gcache.page_size`:

.. index::
   pair: Parameters; gcache.page_size

Size of the page files in page storage. The limit
on overall page storage is the size of the disk.
Pages are prefixed by ``gcache.page``.

.. rubric:: gcache.keep_pages_size

.. _`gcache.keep_pages_size`:

.. index::
   pair: Parameters; gcache.keep_pages_size

Total size of the page storage pages to keep for
caching purposes. If only page storage is enabled,
one page is always present. 


.. rubric:: gcache.mem_size

.. _`gcache.mem_size`:

.. index::
   pair: Parameters; gcache.mem_size

Max size of the ``malloc()`` store (read: RAM). For setups with spare RAM. 

-------------------------------------
 Setting Galera Parameters in MySQL
-------------------------------------
.. _`Setting Galera Parameters in MySQL`:

.. index::
   pair: Parameters; Setting
.. index::
   pair: Parameters; Checking
   
You can set *Galera Cluster* parameters in the *my.cnf*
configuration file as follows:

``wsrep_provider_options="gcs.fc_limit=256;gcs.fc_factor=0.9"``

This is useful in master-slave setups.

You can set *Galera Cluster*
parameters through a MySQL client as follows:

``mysql> SET GLOBAL wsrep_provider_options="evs.send_window=16";``

This command would only change the ``evs.send_window`` value.

To check which parameters are used in *Galera Cluster*,
enter the command below:

``mysql> SHOW VARIABLES LIKE 'wsrep_provider_options';``

.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
