===========================
 Configuring Galera Cluster
===========================
.. _`Configuring Galera Cluster`:

This chapter presents the mandatory and recommended settings
for a Galera cluster. It may be possible to start the cluster after
only setting the ``wsrep_provider`` and ``wsrep_cluster_address``
variables. However, the best results can be achieved by
fine-tuning the configuration to best match the use case.

For more information on the settings, see chapter
:ref:`Galera Parameters <Galera Parameters>`.

-------------------------------
 Example Configuration File
-------------------------------
.. _`Example Configuration File`:

See below for an example *my.cnf* file::

    [mysqld]
    # 1. Mandatory settings: these settings are REQUIRED for proper cluster operation
    query_cache_size=0
    binlog_format=ROW
    default_storage_engine=innodb
    innodb_autoinc_lock_mode=2
    # innodb_doublewrite=1 - this is the default and it should stay this way
    
    # 2. Optional mysqld settings: your regular InnoDB tuning and such
    datadir=/mnt/mysql/data
    innodb_buffer_pool_size=28G
    innodb_log_file_size=100M
    innodb_file_per_table
    innodb_flush_log_at_trx_commit=2

    # 3. wsrep provider configuration: basic wsrep options
    wsrep_provider=/usr/lib64/galera/libgalera_smm.so
    wsrep_provider_options="gcache.size=32G; gcache.page_size=1G"
    wsrep_cluster_address=gcomm://192.168.0.1,192.168.0.2,192.168.0.3
    wsrep_cluster_name='my_galera_cluster'
    wsrep_node_address='192.168.0.2'
    wsrep_node_name='node2'
    wsrep_sst_method=xtrabackup
    wsrep_sst_auth=root:rootpa$$
    
    # 4. additional "frequently used" wsrep settings
    wsrep_node_incoming_address='192.168.10.2'
    wsrep_sst_donor='node3'
    wsrep_slave_threads=16

In the example above, there are 11 *wsrep* configuration variables.
This is usually all that is needed for clustering.

   .. note:: Always customize the settings in section 3
             before taking the cluster into production.

--------------------
 Mandatory Settings
--------------------
.. _`Mandatory Settings`:

You must give values to the settings below:

- ``query_cache_size=0`` |---| This value disables the query cache.
  The query cache is disabled as, in the typical high concurrency
  environments, InnoDB scalability outstrips the query cache.
  It is not recommended to enable the query cache.
- ``binlog_format=ROW`` |---| This variable sets the binary logging
  format to use row-level replication as opposed to statement-level
  replication. Do not change this value, as it affects performance
  and consistency. As a side effect to using this value, binlog, if
  turned on, can be ROW only.
- ``default_storage_engine=InnoDB`` |---| InnoDB is a high-reliability
  and high-performance storage engine for MySQL. Starting with MySQL
  5.5, it is the default MySQL storage engine.
- ``innodb_autoinc_lock_mode=2`` |---| This variable sets the lock mode
  to use for generating auto-increment values. Value 2 sets the interleaved
  lock mode. Without this parameter, ``INSERT``s into tables with an
  ``AUTO_INCREMENT`` column may fail. Lock modes 0 and 1 can cause
  unresolved deadlocks and make the system unresponsive.
  See also chapter `Setting Parallel CPU Threads`_

   .. note:: If you use Galera provider version 2.0 or higher,
             set ``innodb_doublewrite`` to 1 (default).

--------------------------
 Optional MySQL Settings
--------------------------
.. _`Optional MySQL Settings`:

For better performance, you can give values to the settings below:

- ``datadir=/mnt/mysql/data`` |---| The MySQL data directory. 
- ``innodb_buffer_pool_size=28G`` |---| The size in bytes of the buffer
  pool, that is, the memory area where InnoDB caches table and index
  data.
- ``innodb_log_file_size=100M`` |---| The size in bytes of each log file
  in a log group. 
- ``innodb_file_per_table`` |---| When innodb_file_per_table is enabled,
  InnoDB stores the data and indexes for each newly created table in
  a separate .ibd file, rather than in the system tablespace. 
- ``innodb_flush_log_at_trx_commit`` |---| This parameter
  improves performance. The parameter defines how often the
  log buffer is written out to the log file and how often
  the log file is flushed onto disk. When the value is 2,
  the log buffer is written out to the file at each commit,
  but the flush to disk operation is not performed
  on it, but it takes place once per second. 

  Compared with the default value 1, you can achieve better
  performance by setting the value to 2, but an operating system
  crash or a power outage can erase the last second of transactions.
  However, this risk is handled by synchronous replication |---| you
  can always recover the node from another node.

  Set::

    ``innodb_flush_log_at_trx_commit=2``


---------------------------
 wsrep Provider Settings
---------------------------
.. _`wsrep Provider Settings`:

The basic wsrep provider settings are:

- ``wsrep_provider=/usr/lib64/galera/libgalera_smm.so`` |---| The
  path to the Galera plugin.
- ``wsrep_cluster_address=gcomm://192.168.0.1,192.168.0.2,192.168.0.3`` |---| The
  cluster connection URL. See chapter :ref:`Creating a Cluster <Creating a Cluster>`.
- ``wsrep_provider_options="gcache.size=32G; gcache.page_size=1G"`` |---| A
  string of provider options passed directly to provider.
- ``wsrep_cluster_name='my_galera_cluster'`` |---| The logical cluster
  name. If a node tries to connect to a cluster with a different name,
  connection fails
- ``wsrep_node_address='192.168.0.2'`` |---| An option to explicitly
  specify the network address of the node if autoguessing for some
  reason does not produce desirable results.
- ``wsrep_node_name='node2'`` |---| The logical node name for convenience.
- ``wsrep_sst_method=xtrabackup`` |---| The method used for state snapshot transfers.
- ``wsrep_sst_auth=root:rootpa$$`` |---| A string with authentication
  information for state snapshot transfer.
  
For better performance, you can also give values to the settings below:

- ``wsrep_node_incoming_address='192.168.10.2'`` |---| The address at
  which the server expects client connections. This parameter is intended
  for integration with load balancers. 
- ``wsrep_sst_donor='node3'`` |---| The name of the server that should
  be used as a source for state transfer. Give the name as ``wsrep_node_name``.
- ``wsrep_slave_threads=16`` |---| How many threads to use for applying
  slave writsets.

---------------------------
 Optional Memory Settings
---------------------------
.. _`Optional Memory Settings`:

During normal operation a MariaDB Galera node does not consume
much more memory than a regular MariaDB server. Additional
memory is consumed for the certification index and uncommitted
write sets, but usually this is not noticeable in a typical
application. However, writeset caching during state transfer
makes an exception.

When a node is receiving a state transfer, it cannot process
and apply incoming write sets because it has no state to
apply them to yet. Depending on a state transfer mechanism
(for example, *mysqldump*), the node that sends the state
transfer may not be able to apply write sets. Instead, the
node must cache the write sets for a catch-up phase. Currently,
the write sets are cached in memory and, if the system runs out
of memory, either the state transfer will fail or the cluster
will block and wait for the state transfer to end.

To control memory usage for writeset caching, adjust the
Galera parameters below:

- ``gcs.recv_q_hard_limit`` |---| the maximum allowed size of
  recv queue. This should normally be half of (RAM + swap).
  If this limit is exceeded, Galera will abort the server
- ``gcs.recv_q_soft_limit`` |---| A fraction of ``gcs.recv_q_hard_limit``
  after which replication rate will be throttled.
- ``gcs.max_throttle`` |---| How much we can throttle the replication
  rate during state transfer (to avoid running out of memory).

-------------------
 Configuration Tips
-------------------
.. _`Configuration Tips`:

This chapter contains some advanced configuration tips.

Setting Parallel CPU Threads
============================
.. _`Setting Parallel CPU Threads`:

There is no rule about how many slave :abbr:`CPU (Central Processing Unit)`
threads one should configure for replication. At the same time,
parallel threads do not guarantee better performance. However,
parallel applying will not impair regular operation performance
and will most likely speed up the synchronization of new nodes
with the cluster.

Start with four slave threads per core, the logic being that, in a
balanced system, four slave threads can usually saturate the core.
However, depending on IO performance, this figure can be increased
several times (for example, you can use 32 slave threads on a
single-core ThinkPad R51 with a 4200 RPM drive). 

The top limit on the total number of slave threads can be
obtained from the ``wsrep_cert_deps_distance`` status
variable. This value essentially determines how many writesets
on average can be applied in parallel. Do not use a value higher
than that.

To set four parallel CPU threads, use the parameter value below::

    wsrep_slave_threads=4

.. note:: Parallel applying requires the following settings:

          - ``innodb_autoinc_lock_mode=2``
          - ``innodb_locks_unsafe_for_binlog=1``
 
WAN Replication
===============
.. _`WAN Replication`:

Transient network connectivity failures are not rare in
:abbr:`WAN (Wide Area Network)` configurations. Thus, you
may want to increase the keepalive timeouts to avoid
partitioning. The following group of *my.cnf* settings
tolerates 30 second connectivity outages::

  wsrep_provider_options = "evs.keepalive_period = PT3S; evs.inactive_check_period = PT10S; evs.suspect_timeout = PT30S; evs.inactive_timeout = PT1M; evs.install_timeout = PT1M"

Set the ``evs.suspect_timeout`` parameter value as high as possible
to avoid partitions (as partitions will cause state transfers, which
are very heavy). The ``evs.inactive_timeout`` parameter value must
be no less than the ``evs.suspect_timeout`` parameter value and the
``evs.install_timeout`` parameter value must be no less than the
``evs.inactive_timeout`` parameter value.

.. note:: WAN links can have have exceptionally high latencies. Take
          Round-Trip Time (RTT) measurements (ping RTT is a fair estimate)
          from between your cluster nodes and make sure
          that all temporal Galera settings (periods and timeouts, such
          as ``evs.join_retrans_period``) exceed the highest RTT in
          your cluster.
  
Multi-Master Setup
==================
.. _`Multi-Master Setup`:

The more masters (nodes which simultaneously process writes from
clients) are in the cluster, the higher the probability of certification
conflict. This may cause undesirable rollbacks and performance degradation.
In such a case, reduce the number of masters.

Single Master Setup
===================
.. _`Single Master Setup`:

If only one node at a time is used as a master, certain requirements,
such as the slave queue size, may be relaxed. Flow control can be
relaxed by using the settings below::

    wsrep_provider_options = "gcs.fc_limit = 256; gcs.fc_factor = 0.99; gcs.fc_master_slave = yes"

These settings may improve replication performance by
reducing the rate of flow control events. This setting
can also be used as suboptimal in a multi-master setup.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
