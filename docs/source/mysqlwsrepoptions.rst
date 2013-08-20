======================
 MySQL wsrep Options
======================
.. _`MySQL wsrep Options`:
.. index::
   single: Drupal
.. index::
   pair: Logs; Debug log


These are MySQL system variables introduced by *wsrep*
patch v0.8. All variables are global except where marked
by (L).


+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| Option                                | Default                            | Introduced           | Deprecated         | Dynamic  |
+=======================================+====================================+======================+====================+==========+
| :ref:`wsrep_auto_increment_control    | *ON*                               | 1                    | n/a                |          |
| <wsrep_auto_increment_control>`       |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_causal_reads              | *OFF*                              | 1                    | n/a                |          |
| <wsrep_causal_reads>` **(L)**         |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_certify_nonPK             | *ON*                               | 1                    | n/a                |          |
| <wsrep_certify_nonPK>`                |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_cluster_address           |                                    | 1                    | n/a                |          |
| <wsrep_cluster_address>`              |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_cluster_name              | *my_test_cluster*                  | 1                    | n/a                |          |
| <wsrep_cluster_name>`                 |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_convert_LOCK_to_trx       | *OFF*                              | 1                    | n/a                |          |
| <wsrep_convert_LOCK_to_trx>`          |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_data_home_dir             | *<mysql_real_data_home>*           | 1                    | n/a                |          |
| <wsrep_data_home_dir>`                |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_dbug_option               |                                    | 1                    | n/a                |          |
| <wsrep_dbug_option>`                  |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_debug                     | *OFF*                              | 1                    | n/a                |          |
| <wsrep_debug>`                        |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_drupal_282555_workaround  | *ON*                               | 1                    | n/a                |          |
| <wsrep_drupal_282555_workaround>`     |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_forced_binlog_format      | *NONE*                             | 1                    | n/a                |          |
| <wsrep_forced_binlog_format>`         |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_max_ws_rows               | *128K*                             | 1                    | n/a                |          |
| <wsrep_max_ws_rows>`                  |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_max_ws_size               | *1G*                               | 1                    | n/a                |          |
| <wsrep_max_ws_size>`                  |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_node_address              | *<address>[:port]*                 | 1                    | n/a                |          |
| <wsrep_node_address>`                 |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_node_incoming_address     | *<address>[:mysqld_port]*          | 1                    | n/a                |          |
| <wsrep_node_incoming_address>`        |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_node_name                 | *<hostname>*                       | 1                    | n/a                |          |
| <wsrep_node_name>`                    |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_notify_cmd                |                                    | 1                    | n/a                |          |
| <wsrep_notify_cmd>`                   |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_on                        | *ON*                               | 1                    | n/a                |          |
| <wsrep_on>` **(L)**                   |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_OSU_method                | *TOI*                              | Patch version 3      | n/a                |          |
| <wsrep_OSU_method>`                   |                                    | (5.5.17-22.3)        |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_provider                  | *none*                             | 1                    | n/a                |          |
| <wsrep_provider>`                     |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_provider_options          |                                    | 1                    | n/a                |          |
| <wsrep_provider_options>`             |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_retry_autocommit          | *1*                                | 1                    | n/a                |          |
| <wsrep_retry_autocommit>`             |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_slave_threads             | *1*                                | 1                    | n/a                |          |
| <wsrep_slave_threads>`                |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_sst_auth                  |                                    | 1                    | n/a                |          |
| <wsrep_sst_auth>`                     |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_sst_donor                 |                                    | 1                    | n/a                |          |
| <wsrep_sst_donor>`                    |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_sst_donor_rejects_queries | *OFF*                              | 1                    | n/a                |          |
| <wsrep_sst_donor_rejects_queries>`    |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_sst_method                | *mysqldump*                        | 1                    | n/a                |          |
| <wsrep_sst_method>`                   |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_sst_receive_address       | *<wsrep_node_address>*             | 1                    | n/a                |          |
| <wsrep_sst_receive_address>`          |                                    |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+
| :ref:`wsrep_start_position            | *00000000-0000-0000-*              | 1                    | n/a                |          |
| <wsrep_start_position>`               | *0000-000000000000:-1*             |                      |                    |          |
+---------------------------------------+------------------------------------+----------------------+--------------------+----------+


.. rubric:: wsrep_auto_increment_control

.. _`wsrep_auto_increment_control`:

.. index::
   pair: Parameters; wsrep_auto_increment_control

Automatically adjust ``auto_increment_increment`` and
``auto_increment_offset`` system variables when the
cluster membership changes.

This parameters significantly reduces the certification
conflict rate for``INSERT`` clauses.


.. rubric:: wsrep_causal_reads

.. _`wsrep_causal_reads`:

.. index::
   pair: Parameters; wsrep_causal_reads

Enforce strict cluster-wide ``READ COMMITTED`` semantics on
non-transactional reads. Results in larger read latencies. 


.. rubric:: wsrep_certify_nonPK

.. _`wsrep_certify_nonPK`:

.. index::
   pair: Parameters; wsrep_certify_nonPK

Generate primary keys for rows without them for the
purpose of certification. This is required for parallel
applying. Do not use tables without primary keys. 

.. rubric:: wsrep_cluster_address

.. _`wsrep_cluster_address`:

.. index::
   pair: Parameters; wsrep_cluster_address
.. index::
   single: my.cnf

*Galera Cluster* takes addresses in the URL format::

    <backend schema>://<cluster address>[?option1=value1[&option2=value2]]

For example::

    gcomm://192.168.0.1:4567?gmcast.listen_addr=0.0.0.0:5678 

Changing this variable in runtime will cause the node to
close connection to the current cluster (if any), and
reconnect to the new address. (However, doing this at
runtime may not be possible for all SST methods.) As of
*Galera Cluster* 23.2.2,
it is possible to provide a comma separated
list of other nodes in the cluster as follows::

    gcomm://node1:port1,node2:port2,...[?option1=value1&...]

Using the string *gcomm://* without any address will cause
the node to startup alone, thus initializing a new cluster
(that the other nodes can join to).

.. note: Never use an empty ``gcomm://`` string in *my.cnf*. If a node restarts,
         that will cause the node to not join back to the cluster that it
         was part of, rather it will initialize a new one node cluster
         and cause a split brain. To bootstrap a cluster, you should
         only pass the ``gcomm://`` string on the command line, such as:
         
         ``service mysql start --wsrep-cluster-address="gcomm://"``


.. rubric:: wsrep_cluster_name

.. _`wsrep_cluster_name`:

.. index::
   pair: Parameters; wsrep_cluster_name

The logical cluster name. If a node tries to connect to a
cluster with a different name, the connection fails. The
cluster name must be same on all the cluster nodes. 

 
.. rubric:: wsrep_convert_LOCK_to_trx

.. _`wsrep_convert_LOCK_to_trx`:

.. index::
   pair: Parameters; wsrep_convert_LOCK_to_trx

Convert ``LOCK/UNLOCK TABLES`` statements to ``BEGIN/COMMIT`` statements.
In other words, this parameter implicitly converts locking sessions into
transactions within *mysqld*. By itself, it does not mean support for
locking sessions, but it prevents the database from ending up in a logically
inconsistent state.

Sometimes this parameter may help to get old applications
working in a multi-master setup.

.. note:: Loading a large database dump with ``LOCK``
          statements can result in abnormally large transactions and
          cause an out-of-memory condition.

.. rubric:: wsrep_data_home_dir

.. _`wsrep_data_home_dir`:

.. index::
   pair: Parameters; wsrep_data_home_dir

A directory where the wsrep provider will store its files.
*Galera Cluster* uses this parameter
to store its internal state.

.. rubric:: wsrep_dbug_option

.. _`wsrep_dbug_option`:

.. index::
   pair: Parameters; wsrep_dbug_option

A debug option to be passed to the provider.


.. rubric:: wsrep_debug

.. _`wsrep_debug`:

.. index::
   pair: Parameters; wsrep_debug

Enable debug log output.


.. rubric:: wsrep_drupal_282555_workaround

.. _`wsrep_drupal_282555_workaround`:

.. index::
   pair: Parameters; wsrep_drupal_282555_workaround

Enable a workaround for Drupal (actually MySQL/InnoDB) bug
#282555 (Inserting a ``DEFAULT`` value into an
``AUTO_INCREMENT`` column may return a duplicate key error).

Documented at:

- http://bugs.mysql.com/bug.php?id=41984
- http://drupal.org/node/282555

.. rubric:: wsrep_forced_binlog_format

.. _`wsrep_forced_binlog_format`:

.. index::
   pair: Parameters; wsrep_forced_binlog_format

Force every transaction to use the given binlog format. When
this variable is set to something else than *NONE*, all
transactions will use the given forced format, regardless of
the client session specified in ``binlog_format``.

Valid choices for ``wsrep_forced_binlog_format`` are: *ROW*,
*STATEMENT*, *MIXED* and the special value *NONE*,
meaning that there is no forced binlog format in effect.

This variable was introduced to support ``STATEMENT`` format
replication during  rolling schema upgrade processing.
However, in most cases, ``ROW`` replication
is valid for asymmetric schema replication.



.. rubric:: wsrep_max_ws_rows

.. _`wsrep_max_ws_rows`:

.. index::
   pair: Parameters; wsrep_max_ws_rows

The maximum number of rows allowed in the writeset. Currently,
this parameter limits the supported size of transactions
and ``LOAD DATA`` statements.


.. rubric:: wsrep_max_ws_size

.. _`wsrep_max_ws_size`:

.. index::
   pair: Parameters; wsrep_max_ws_size

The maximum allowed writeset size. Currently, this parameter
limits the supported size of transactions and ``LOAD DATA``
statements.

The maximum allowed writeset size is 2G.


.. rubric:: wsrep_node_address

.. _`wsrep_node_address`:


.. index::
   pair: Parameters; wsrep_node_address

An option to explicitly specify the network address of the
node, if autoguessing for some reason does not produce
desirable results (multiple network interfaces, NAT, etc.)

By default, the address of the first network interface (*eth0*)
and the default port 4567 are used. The *<address>* and
*:port* will be passed to the Galera replication Plugin to be
used as a base address in its communications. It will also be
used to derive the default values for parameters
``wsrep_sst_receive_address`` and ``ist.recv_address``.


.. rubric:: wwsrep_node_incoming_address

.. _`wsrep_node_incoming_address`:


.. index::
   pair: Parameters; wsrep_node_incoming_address

The address at which the server expects client connections.
Intended for integration with load balancers. Not used for now.

.. rubric:: wsrep_node_name

.. _`wsrep_node_name`:


.. index::
   pair: Parameters; wsrep_node_name

The logical node name - for convenience.

.. rubric:: wsrep_notify_cmd

.. _`wsrep_notify_cmd`:

.. index::
   pair: Parameters; wsrep_notify_cmd

This command is run whenever the cluster membership or state
of this node changes. This option can be used to (re)configure
load balancers, raise alarms, and so on. The command passes on
one or more of the following options:

--status <status str>        The status of this node. The possible statuses are:

                             - *Undefined* |---| The node has just started up 
                               and is not connected to any :term:`Primary Component`
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
                               provider automatically assigns this ID for each node.
                             - ``<node name>`` |---| The node name as it is set in the
                               ``wsrep_node_name`` option.
                             - ``<incoming address>`` |---| The address for client
                               connections as it is set in the ``wsrep_node_incoming_address``
                               option.

--index                      The index of this node in the node list.

Click this link
`link <http://bazaar.launchpad.net/~codership/codership-mysql/wsrep-5.5/view/head:/support-files/wsrep_notify.sh>`_ 
to view an example script that updates two tables
on the local node with changes taking place at the
cluster.


.. rubric:: wsrep_on

.. _`wsrep_on`:


.. index::
   pair: Parameters; wsrep_on

Use wsrep replication. When switched off, no changes made in
this session will be replicated.


.. rubric:: wsrep_OSU_method

.. _`wsrep_OSU_method`:

.. index::
   pair: Parameters; wsrep_OSU_method

Online schema upgrade method (MySQL >= 5.5.17). See also
:ref:`Schema Upgrades <Schema Upgrades>`.

Online Schema Upgrade (OSU) can be performed with two
alternative methods:

- *Total Order Isolation* (TOI) runs the DDL statement in all
  cluster nodes in the same total order sequence, locking the
  affected table for the duration of the operation. This may
  result in the whole cluster being blocked for the duration
  of the operation.
- *Rolling Schema Upgrade* (RSU) executes the DDL statement
  only locally, thus blocking one cluster
  node only. During the DDL processing, the node is
  not replicating and may be unable to process replication
  events (due to a table lock). Once the DDL operation is
  complete, the node will catch up and sync with the cluster
  to become fully operational again. The DDL statement or its
  effects are not replicated; the user is responsible for
  manually performing this operation on each of the nodes.


.. rubric:: wsrep_provider

.. _`wsrep_provider`:


.. index::
   pair: Parameters; wsrep_provider

A path to wsrep provider to load. If not specified, all calls
to wsrep provider will be bypassed and the server
behaves like a regular *mysqld* server.
   
.. rubric:: wsrep_provider_options

.. _`wsrep_provider_options`:

.. index::
   pair: Parameters; wsrep_provider_options

A string of provider options passed directly to the provider.

Usually, you just fine-tune:

- ``gcache.size``, that is, the size of the GCache ring buffer,
  which is used for Incremental State Transfer, among other
  things. See chapter :ref:`Galera Parameters <Galera Parameters>`.
- Group communication timeouts. See chapter
  :ref:`WAN Replication <WAN Replication>`.

  See also a list of all *Galera Cluster* parameters
  in chapter :ref:`Galera Parameters <Galera Parameters>`.


.. rubric:: wsrep_retry_autocommit

.. _`wsrep_retry_autocommit`:

.. index::
   pair: Parameters; wsrep_retry_autocommit

If an autocommit query fails the certification test due to a
cluster-wide conflict, we can retry it without returning an
error to the client. This option sets how many times to retry.

This option is analogous to rescheduling an autocommit query
should it go into deadlock with other transactions
in the database lock manager.


.. rubric:: wsrep_slave_threads

.. _`wsrep_slave_threads`:

.. index::
   pair: Parameters; wsrep_slave_threads

How many threads to use for applying slave writesets. There
are two things to consider when choosing the number:

1. The number should be at least two times the number of CPU
   cores.
2. Consider how many writing client connections the other
   nodes would have. Divide this by four and use that as the
   ``wsrep_slave_threads`` value.


.. rubric:: wsrep_sst_auth

.. _`wsrep_sst_auth`:

.. index::
   pair: Parameters; wsrep_sst_auth

A string with authentication information for state snapshot
transfer. The string depends on the state transfer method. For
the *mysqldump* state transfer, it is *<username>:<password>*,
where *username* has root privileges on this server. The
*rsync* method ignores this option.

Use the same value on all nodes. This parameter is used to
authenticate with both the state snapshot receiver and the
state snapshot donor.



.. rubric:: wsrep_sst_donor

.. _`wsrep_sst_donor`:

.. index::
   pair: Parameters; wsrep_sst_donor

A name (given in the ``wsrep_node_name`` parameter) of the server
that should be used as a source for state transfer. If not
specified, *Galera Cluster* will choose
the most appropriate one.

In this case, the group communication module monitors the node
state for the purpose of flow control, state transfer and quorum
calculations. The node can be a if it is in the ``SYNCED`` state.
The first node in the ``SYNCED`` state in the index becomes the
donor and is not available for requests. 

If there are no free ``SYNCED`` nodes at the moment, the
joining node reports::

    Requesting state transfer failed: -11(Resource temporarily unavailable).
    Will keep retrying every 1 second(s)

and keeps on retrying the state transfer request until it
succeeds. When the state transfer request succeeds, the
entry below is written to log:

``Node 0 (XXX) requested state transfer from '*any*'. Selected 1 (XXX) as donor.``

.. rubric:: wsrep_sst_donor_rejects_queries

.. _`wsrep_sst_donor_rejects_queries`:

.. index::
   pair: Parameters; wsrep_sst_donor_rejects_queries

.. index::
   pair: Errors; ER_UNKNOWN_COM_ERROR

This parameter prevents blocking client sessions on a
donor if the donor is performing a blocking SST, such
as *mysqldump* or *rsync*.

In these situations, all queries return error
``ER_UNKNOWN_COM_ERROR, "Unknown command"`` like a joining
node does. In this case, the client (or the JDBC driver) can
reconnect to another node.

.. note:: As SST is scriptable, there is no way to tell whether
          the requested SST method is blocking or not. You may
          also want to avoid querying the donor even with
          non-blocking SST. Consequently, this variable will
          reject queries on the donor regardless of the SST
          (that is, also for *xtrabackup*) even if the initial
          request concerned a blocking-only SST.

.. note:: The *mysqldump* SST does not work with this setting,
          as *mysqldump* must run queries on the donor and there
          is no way to distinguish a *mysqldump* session from a
          regular client session. 


.. rubric:: wsrep_sst_method

.. _`wsrep_sst_method`:

.. index::
   pair: Parameters; wsrep_sst_method

The method to use for state snapshot transfers. The
``wsrep_sst_<wsrep_sst_method>`` command will be called with
the following arguments. For more information, see also
:ref:`Scriptable State Snapshot Transfer
<Scriptable State Snapshot Transfer>`.

The supported methods are:

- *mysqldump* |---| This is a slow (except for small datasets),
  but the most tested option.
- *rsync* |---| This option is much faster than *mysqldump* on
  large datasets.
- *rsync_wan* |---| This option is almost the same as *rsync*,
  but uses the *delta-xfer* algorithm to minimize
  network traffic.

  .. note::  You can only use *rsync* when a node is starting.
             In other words, you cannot use *rsync* under a running InnoDB
             storage engine.
- *xtrabackup* |---| This option is a fast and practically
  non-blocking SST method based on Percona's xtrabackup tool.

  If you want to use *xtrabackup*, the following settings must
  be present in the *my.cnf* configuration file on all nodes::

      [mysqld]
      wsrep_sst_auth=root:<root password>
      datadir=<path to data dir>
      [client]
      socket=<path to socket>


.. rubric:: wsrep_sst_receive_address

.. _`wsrep_sst_receive_address`:

.. index::
   pair: Parameters; wsrep_sst_receive_address

The address at which this node expects to receive state
transfers. Depends on the state transfer method. For example,
for the *mysqldump* state transfer, it is the address and the
port on which this server listens. By default this is set to
the *<address>* part of ``wsrep_node_address``.

.. note:: Check that your firewall allows connections to this
          address from other cluster nodes.
  


.. rubric:: wsrep_start_position

.. _`wsrep_start_position`:

.. index::
   pair: Parameters; wsrep_start_position

This variable exists for the sole purpose of notifying a joining
node about state transfer completion. For more information, see
:ref:`Scriptable State Snapshot Transfer <Scriptable State Snapshot Transfer>`.

.. rubric:: wsrep_ws_persistency

.. _`wsrep_ws_persistency`:

.. index::
   pair: Parameters; wsrep_ws_persistency

Whether to store writesets locally for debugging. Not used in 0.8.


.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
