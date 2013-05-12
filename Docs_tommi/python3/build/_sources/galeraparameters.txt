==================
 Galera Parameters
==================
.. _`Galera Parameters`:

As of version 0.8 Galera accepts parameters as semicolon-separated
key value pair lists, such as ``key1 = value1; key2 = value2``.
In this way, you can configure an arbitrary number of Galera parameters
in one call. A key consists of parameter group and parameter name:

``<group>.<name>``

Where ``<group>`` roughly corresponds to some Galera module.

Table legend:

- *Numeric values* |---| Galera understand the following numeric modifiers:
  K, M, G, T standing for |210|, |220|, |230| and |240| respectively.
- *Boolean values* |---| Galera accepts the following boolean values: 0, 1, yes, no, true, false, on, off.
- Time periods must be expressed in the ISO8601 format. See also the examples below.
- ``(R)`` marks parameters that can be changed during runtime.

.. |210| replace:: 2\ :sup:`10`\
.. |220| replace:: 2\ :sup:`20`\
.. |230| replace:: 2\ :sup:`30`\
.. |240| replace:: 2\ :sup:`40`\

A test on documenting options in a table:

+---------------------------------------+-----------------------+----------------------------------------------------+
| Parameter                             | Default               | Description                                        |
+=======================================+=======================+====================================================+
| ``protonet.backend``                  | *asio*                | Which transport backend to use. Currently only     |
|                                       |                       | ASIO is supported.                                 |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``protonet.version``                  | *0*                   |                                                    |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``socket.ssl_cert``                   |                       | A path (absolute or relative to the working dir)   |
|                                       |                       | to an SSL certificate (in PEM format).             |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``socket.ssl_key``                    |                       | A path (absolute or relative to the working dir) to|
|                                       |                       | a private key for a certificate (in PEM format).   |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``socket.ssl_compression``            | *yes*                 | Whether to enable compression on SSL connections.  |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``socket.ssl_cipher``                 | *AES128-SHA*          | Symmetric cipher to use. AES128 is used by default |
|                                       |                       | it is considerably faster and no less secure than  |
|                                       |                       | AES256.                                            |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``gmcast.listen_addr``                | *tcp://0.0.0.0:4567*  | Address at which Galera listens to connections     |
|                                       |                       | from other nodes. By default the port to listen at |
|                                       |                       | is taken from the connection address. This setting |
|                                       |                       | can be used to overwrite that.                     |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``gmcast.mcast_addr``                 |                       | If set, UDP multicast will be used for replication,|
|                                       |                       | for example::                                      |
|                                       |                       |                                                    |
|                                       |                       |   ``gmcast.mcast_addr=239.192.0.11``               |
|                                       |                       |                                                    |
|                                       |                       | The value must be the same on all nodes.           |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``gmcast.mcast_ttl``                  | *1*                   | Time to live value for multicast packets.          |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``gmcast.peer_timeout``               | *PT3S*                | Connection timeout to initiate message relaying.   |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``gmcast.time_wait``                  | *PT5S*                | Time to wait until allowing peer declared outside  |
|                                       |                       | of stable view to reconnect.                       |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``gmcast.version``                    | *0*                   |                                                    |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.causal_keepalive_period``       |                       | For developer use only. Defaults to                |
|                                       |                       | ``evs.keepalive_period``.                          |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.consensus_timeout``             | *PT30S*               | Timeout on reaching the consensus about cluster    |
|                                       |                       | membership.                                        |
|                                       |                       |                                                    |
|                                       |                       | **Deprecated** See ``evs.install_timeout``.        |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.debug_log_mask`` **(R)**        | *0x1*                 | Control EVS debug logging, only effective when     |
|                                       |                       | ``wsrep_debug`` is in use.                         |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.inactive_check_period``         | *PT1S*                | How often to check for peer inactivity.            |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.inactive_timeout``              | *PT15S*               | Hard limit on the inactivity period, after which   |
|                                       |                       | the node is pronounced dead.                       |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.info_log_mask``                 | *0*                   | Control extra EVS info logging. Bits:              |
|                                       |                       |                                                    |
|                                       |                       | - *0x1* |---| extra view change info               |
|                                       |                       | - *0x2* |---| extra state change info              |
|                                       |                       | - *0x4* |---| statistics                           |
|                                       |                       | - *0x8* |---| profiling (only in builds with       |
|                                       |                       |         profiling enabled)                         |
|                                       |                       |                                                    |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.install_timeout`` **(R)**       | *PT15S*               | Timeout on waiting for install message             |
|                                       |                       | acknowledgments. Successor to                      |
|                                       |                       | ``evs.consensus_timeout``.                         |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.join_retrans_period`` **(R)**   | *PT1S*                | How often to retransmit EVS join messages when     |
|                                       |                       | forming cluster the membership.                    |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.keepalive_period``              | *PT1S*                | How often to emit keepalive beacons (in the        |
|                                       |                       | absence of any other traffic).                     |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.max_install_timeouts``          | *1*                   | How many membership install rounds to try before   |
|                                       |                       | giving up (total rounds will be                    |
|                                       |                       | ``evs.max_install_timeouts`` + 2).                 |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.send_window`` **(R)**           | *4*                   | Maximum packets in replication at a time. For WAN  |
|                                       |                       | setups may be set considerably higher, e.g. 512.   |
|                                       |                       | Must be no less than ``evs.user_send_window``.     |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.stats_report_period``           | *PT1M*                | Control period of EVS statistics reporting.        |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.suspect_timeout``               | *PT5S*                | Inactivity period after which the node is          |
|                                       |                       | *suspected* to be dead. If all remaining nodes     |
|                                       |                       | agree on that, the node is dropped out of cluster  |
|                                       |                       | before ``evs.inactive_timeout`` is reached.        |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.use_aggregate``                 | *true*                | Aggregate small packets into one, when possible.   |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.user_send_window`` **(R)**      | *2*                   | Maximum data packets in replication at a time.     |
|                                       |                       | For WAN setups, this calue can be set considerably |
|                                       |                       | higher, to, for example, 512.                      |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.view_forget_timeout``           | *PT5M*                | Drop past views from the view history after this   |
|                                       |                       | timeout.                                           |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``evs.version``                       | *0*                   |                                                    |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``pc.bootstrap``                      |                       | If you set this value to *true* is a signal to     |
|                                       |                       | turn a ``NON-PRIMARY`` compoment into ``PRIMARY``. |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``pc.checksum``                       | *true*                | Checksum replicated messages.                      |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``pc.ignore_sb`` **(R)**              | *false*               | Should we allow nodes to process updates even in   | 
|                                       |                       | the case of split brain? This is a dangerous       |
|                                       |                       | setting in multi-master setup, but should simplify |
|                                       |                       | things in master-slave cluster (especially if only |
|                                       |                       | 2 nodes are used).                                 |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``pc.ignore_quorum`` **(R)**          | *false*               | Completely ignore quorum calculations. For         |
|                                       |                       | example if the master splits from several slaves   |
|                                       |                       | it still remains operational. Use with extreme     |
|                                       |                       | caution even in master-slave setups, as slaves     |
|                                       |                       | will not automatically reconnect to master in this |
|                                       |                       | case                                               |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``pc.linger``                         | *PT2S*                | The period for which the PC protocol waits for the |
|                                       |                       | EVS termination.                                   |
+---------------------------------------+-----------------------+----------------------------------------------------+
| ``pc.npvo``                           | ``false``             | If set to ``true``, the more recent primary        |
|                                       |                       | component overrides older ones in the case of      |
|                                       |                       | conflicting primaries.                             |
+---------------------------------------+-----------------------+----------------------------------------------------+
|                                       |                       |                                                    |
+---------------------------------------+-----------------------+----------------------------------------------------+



A test on documenting options in man page style:


-------------------
 Status Variables
-------------------
.. _`Status Variables`:

``wsrep_notify_cmd``

This command is run whenever the cluster membership or state
of this node changes. This option can be used to (re)configure
load balancers, raise alarms, and so on. The command passes on
one or more of the following options:

--status <status str>        The status of this node. The possible statuses are:

                             - *Undefined* |---| The node has just started up 
                               and is not connected to any primary component
                             - *Joiner* |---| The node is connected to a primary
                               component and now is receiving state snapshot.
                             - *Donor* |---| The node is connected to primary
                               component and now is sending state snapshot.
                             - *Joined* |---| The node has a complete state and
                               now is catching up with the cluster.  
                             - *Synced* |---| The node has synchronized itself
                               with the cluster.
                             - *Error(<error code if available>)* |---| The node
                               is in an error state.
                                
--uuid <state UUID>          The cluster state UUID.
--primary <yes/no>           Whether the current cluster component is primary or not.
--members <list>             A comma-separated list of the component member UUIDs.
                             The members are presented in the following syntax: 
                            
                             - ``<node UUID>`` |---| A unique node ID. The wsrep
                               provider automatically assigns tjhis ID for each node.
                             - ``<node name>`` |---| The node name as it is set in the
                               ``wsrep_node_name`` option.
                             - ``<incoming address>`` |---| The address for client
                               connections as it is set in the ``wsrep_node_incoming_address``
                               option.

Click this link `link <http://bazaar.launchpad.net/~codership/codership-mysql/wsrep-5.5/view/head:/support-files/wsrep_notify.sh>`_ 
to view an example script that updates two tables
on the local node with changes taking place at the
cluster.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
   
