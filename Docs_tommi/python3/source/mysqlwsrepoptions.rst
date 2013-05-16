======================
 MySQL wsrep Options
======================
.. _`MySQL wsrep Options`:

These are MySQL system variables introduced by *wsrep*
patch v0.8. All variables are global except where marked
by (L).

+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| Option                               | Default                   | Description                                                                     |
+======================================+===========================+=================================================================================+
| ``wsrep_auto_increment_control``     | *ON*                      | Automatically adjust ``auto_increment_increment`` and                           |
|                                      |                           | ``auto_increment_offset`` system variables when the                             |
|                                      |                           | cluster membership changes.                                                     |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_causal_reads`` **(L)**       | *OFF*                     | Enforce strict cluster-wide *read committed* semantics on                       |
|                                      |                           | non-transactional reads. Results in larger read latencies.                      |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_certify_nonPK``              | *ON*                      | Generate primary keys for rows without them for the                             |
|                                      |                           | purpose of certification. This is required for parallel                         |
|                                      |                           | applying. We don't recommend using tables without primary                       |
|                                      |                           | keys.                                                                           |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_cluster_address``            |                           | Address to connect to cluster. Galera takes addresses in                        |
|                                      |                           | the URL format::                                                                |
|                                      |                           |                                                                                 |
|                                      |                           |   <backend schema>://<cluster address>                                          |
|                                      |                           |   [?option1=value1[&option2=value2]]                                            |
|                                      |                           |                                                                                 |
|                                      |                           | For example::                                                                   |
|                                      |                           |                                                                                 |
|                                      |                           |   gcomm://192.168.0.1:4567?gmcast.listen_addr=0.0.0.0:5678                      |
|                                      |                           |                                                                                 |
|                                      |                           | Changing this variable in runtime will cause the node to                        |
|                                      |                           | close connection to the current cluster (if any) and                            |
|                                      |                           | reconnect to the new address. (However, doing this at                           |
|                                      |                           | runtime may not be possible for all SST methods.) As of                         |
|                                      |                           | Galera 23.2.2 it is possible to provide a comma separated                       |
|                                      |                           | list of other nodes in the cluster::                                            |
|                                      |                           |                                                                                 |
|                                      |                           |   gcomm://node1:port1,node2:port2,...[?option1=value1&...]                      |
|                                      |                           |                                                                                 |
|                                      |                           | Using the string *gcomm://* without any address will cause                      |
|                                      |                           | the node to startup alone, thus initializing a new cluster                      |
|                                      |                           | (that other nodes can join). Note: You should never use the                     |
|                                      |                           | empty *gcomm://* string in my.cnf. If a node restarts, that                     |
|                                      |                           | will cause the node to not join back to the cluster that it                     |
|                                      |                           | was part of, rather it will initialize a new one node cluster                   |
|                                      |                           | and cause split brain. To bootstrap a cluster, you should only                  |
|                                      |                           | pass the *gcomm://* string on the command line, such as::                       |
|                                      |                           |                                                                                 |
|                                      |                           |   service mysql start --wsrep-cluster-address="gcomm://"                        |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_cluster_name``               | *my_test_cluster*         | Logical cluster name. If a node tries to connect to a cluster                   |
|                                      |                           | with a different name, the connection fails.                                    |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_convert_LOCK_to_trx``        | *OFF*                     | Convert ``LOCK/UNLOCK TABLES`` statements to ``BEGIN/COMMIT.``                  |
|                                      |                           | Sometimes this may help to get old applications working in                      |
|                                      |                           | multi-master setup. Use with caution, this option may result                    |
|                                      |                           | in huge writesets.                                                              |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_data_home_dir``              | *<mysql_real_data_home>*  | A directory where wsrep provider will store its files.                          |
|                                      |                           | Galera uses it to store its internal state.                                     |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_dbug_option``                |                           | A DBUG option to be passed to the provider.                                     |
|                                      |                           |                                                                                 |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_debug``                      | *OFF*                     | Enable debug log output.                                                        |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_drupal_282555_workaround``   | *ON*                      | Enable workaround for Drupal (actually MySQL/InnoDB) bug                        |
|                                      |                           | #282555 when inserting DEFAULT value into an                                    |
|                                      |                           | ``AUTO_INCREMENT`` column may return a duplicate key error.                     |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_max_ws_rows``                | *128K*                    | Maximum number of rows allowed in the writeset. Currently it                    |
|                                      |                           | limits the supported size of transactions and ``LOAD DATA``                     |
|                                      |                           | statements.                                                                     |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_max_ws_size``                | *1G*                      | Maximum allowed writeset size. Currently it limits the                          |
|                                      |                           | supported size of transactions and ``LOAD DATA`` statements.                    |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_node_address``               | *<address>[:port]*        | An option to explicitly specify the network address of the                      |
|                                      |                           | node if autoguessing for some reason does not produce                           |
|                                      |                           | desirable results (multiple network interfaces, NAT, etc.) By                   |
|                                      |                           | default the address of the first network interface (*eth0*) is                  |
|                                      |                           | used, and the default port is 4567. The *<address>* and                         |
|                                      |                           | *:port* will be passed to the wsrep provider (Galera) to be                     |
|                                      |                           | used as base address in its communications. It will also be                     |
|                                      |                           | used to derive default values for                                               |
|                                      |                           | ``wsrep_sst_receive_address`` and ``ist.recv_address``.                         |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_node_incoming_address``      | *<address>[:mysqld_port]* | Address at which server expects client connections. Intended                    |
|                                      |                           | for integration with load balancers. Not used for now.                          |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_node_name``                  | *<hostname>*              | Logical node name - for convenience.                                            |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_notify_cmd``                 |                           | A command to run when cluster membership or state of this node                  |
|                                      |                           | changes. See also the notification command arguments in the                     |
|                                      |                           | :ref:`Notification Command Arguments <Notification Command Arguments>`.         |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_on`` **(L)**                 | *ON*                      | Use wsrep replication. When turned off, no changes made in                      |
|                                      |                           | this session will be replicated.                                                |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_OSU_method``                 | *TOI*                     | Online schema upgrade method (MySQL >= 5.5.17). See also                        |
|                                      |                           | :ref:`Rolling Schema Upgrade <Rolling Schema Upgrade>`.                         |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_provider``                   | *none*                    | A path to wsrep provider to load. If not specified, all calls                   |
|                                      |                           | to wsrep provider will be bypassed.                                             |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_provider_options``           |                           | A string of provider options passed directly to provider.                       |
|                                      |                           |                                                                                 |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_retry_autocommit``           | *1*                       | If an autocommit query fails due to cluster-wide conflict we                    |
|                                      |                           | can retry it without returning error to client. This option                     |
|                                      |                           | sets how many times to retry.                                                   |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_slave_threads``              | *1*                       | How many threads to use for applying slave writsets. There                      |
|                                      |                           | are two things to consider when choosing the number:                            |
|                                      |                           |                                                                                 |
|                                      |                           | 1. The number should be at least two times the number of CPU                    |
|                                      |                           |    cores.                                                                       |
|                                      |                           | 2. Consider how many writing client connections the other                       |
|                                      |                           |    nodes would have. Divide this by four and use that as the                    |
|                                      |                           |    ``wsrep_slave_threads`` value.                                               |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_sst_auth``                   |                           | A string with authentication information for state snapshot                     |
|                                      |                           | transfer. The string depends on the state transfer method.                      |
|                                      |                           | For the *mysqldump* state transfer, it is *<user>:<password>*,                  |
|                                      |                           | where *user* has root privileges on this server. The *rsync*                    |
|                                      |                           | method ignores this option.                                                     |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_sst_donor``                  |                           | A name (given in the "wsrep_node_name" option) of the server                    |
|                                      |                           | that should be used as a source for state transfer. If not                      |
|                                      |                           | specified, Galera will choose the most appropriate one.                         |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_sst_method``                 | *mysqldump*               | The method to use for state snapshot transfers. The                             |
|                                      |                           | ``wsrep_sst_<wsrep_sst_method>`` command will be called with                    |
|                                      |                           | the following arguments. For more information, see also                         |
|                                      |                           | :ref:`Scriptable State Snapshot Transfer <Scriptable State Snapshot Transfer>`. |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_sst_receive_address``        | *<wsrep_node_address>*    | The address at which this node expects to receive state                         |
|                                      |                           | transfer. Depends on state transfer method. For example, for                    |
|                                      |                           | *mysqldump* state transfer it is the address and the port on                    |
|                                      |                           | which this server listens. By default this is set to the                        |
|                                      |                           | *<address>* part of ``wsrep_node_address``.                                     |
|                                      |                           |                                                                                 |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_start_position``             | *00000000-0000-0000-*     | This variable exists for the sole purpose of notifying joining                  |
|                                      | *0000-000000000000:-1*    | node about state transfer completion. For more information, see                 |
|                                      |                           | :ref:`Scriptable State Snapshot Transfer <Scriptable State Snapshot Transfer>`. |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+
| ``wsrep_ws_persistency``             | *OFF*                     | Whether to store writesets locally for debugging. Not used in 0.8.              |
+--------------------------------------+---------------------------+---------------------------------------------------------------------------------+


-------------------------------
 Notification Command Arguments
-------------------------------
.. _`Notification Command Arguments`:

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

Click this link
`link <http://bazaar.launchpad.net/~codership/codership-mysql/wsrep-5.5/view/head:/support-files/wsrep_notify.sh>`_ 
to view an example script that updates two tables
on the local node with changes taking place at the
cluster.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
