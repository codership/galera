================================================
Configuring the Database
================================================
.. _`Database Configuration for Galerea Cluster`

Once you finish installing Galera Cluster and setting up your system, you are ready to configure the database itself to serve as a node on a cluster.  

In your configuration file, (my.cnf or my.ini, depending on your system), there are a series of write-set replication variables that you must set before adding the node to the cluster.  For example:

.. code-block:: ini

	[mysqld]
	
	# Mandatory Settings
	binlog_format=ROW
	default_storage_engine=InnoDB
	innodb_autoinc_lock_mode=2
	
	# Optional mysqld Settings
	datadir=/path/to/datadir
	innodb_buffer_pool_size=28G
	innodb_log_file_size=100M
	innodb_file_per_table=1
	innodb_flush_log_at_trx_commit=2
	
	# Basic wsrep Provider Settings
	wsrep_provider=/usr/lib/galera/libgalera_smm.so
	wsrep_provider_options="gcache.szie=32G;gcache.page_size=1G;"
	wsrep_cluster_address=gcomm://192.168.9.1,192.168.0.2,192.168.0.3
	wsrep_cluster_name='example_cluster'
	wsrep_node_address='192.168.0.2'
	wsrep_node_name='example_node2'
	wsrep_sst_method=xtrabackup
	wsrep_sst_auth=root:rootpa$$
	
	# Optional wsrep Provider Settings
	wsrep_node_incoming_address='192.168.10.2'
	wsrep_sst_donor='example_node3'
	wsrep_slave_threads=16
	
	# Optional Memory Settings
	gcs.recv_q_hard_limit=4G
	gcs.recv_q_soft_limit=2G
	gcs.max_throttle=0.25T

These are the primary variables you must set for clustering.

  .. note:: Always customize the wsrep Provider Configuration settings before taking the cluster into production.

---------------------------------------
Mandatory Database Settings
---------------------------------------
.. _`Mandatory DB Settings`:

Galera Cluster requires that you assign the following values for these configuration variables:

- ``binlog_format=ROW``
  
  This sets the binary logging format to use row-level replication as opposed to statement-level replication.

  Do not change this value, as it affects performance and consistency.  The binlog can only use row-level replication.

- ``default_storage_engine=InnoDB``
  
  This sets the default storage engine to InnoDB.

- ``innodb_autoinc_lock_mode=2``
  
  This sets the lock mode for generating auto-increment values to interleaved lock mode.

  Do not change this value as it may cause ``INSERT`` statements on tables with ``AUTO_INCREMENT`` columns to fail. When the lock mode is set to traditional (``0``) or consecutive (``1``) it can cause unresolved deadlocks and make the system unresponsive.

------------------------------------
Optional Database Settings
------------------------------------
.. _`Optional DB Settings`:

For better performance, consider using the following values for these configuration variables:

- ``datadir=/path/to/datadir``
  
  This sets the path to the directory for your database to use.

- ``innodb_buffer_pool_size=28G``
  
  This sets the size in bytes of the buffer pool, the memory area where InnoDB caches table and index data.

- ``innodb_log_file_size=100M``
  
  This sets the size in bytes of each log file in a log group.

- ``innodb_file_per_table=1``
  
  This sets InnoDB to store data and indexes for each newly created table in a separate ``.ibd`` file, rather than in the system tablespace.

- ``innodb_flush_log_at_trx_commit=2``
  
  This sets the log buffer to be written out to file on each commit, but leaves the flush to disk operation to take place once per second.

    .. warning:: Setting ``innodb_flush_log_at_trx_commit`` to a value of ``2`` enables better performance, but an operating system crash or power outage can erase the last second of transactions.  While normally, you can recover this data from another node, it can still be lost if the entire cluster goes down at the same time, such as in the event of a datacenter power outage.

------------------------------------
Basic wsrep Provider Settings
------------------------------------
.. _`Basic wsrep Provider Settings`:

The following values configures the basic wsrep provider settings for your cluster:

  .. note:: Always customize these settings before taking your cluster into production.

- ``wsrep_provider=/path/to/galera/libgalera_smm.so``
  
  This sets the path to the Galera Replication plugin.

- ``wsrep_cluster_address=gcomm://192.168.0.1,192.168.0.2,192.168.0.3``
  
  This sets the cluster connection URL.

- ``wsrep_provider_options="gcache.size=32G; gachce.page_size=1G"``
  
  This sets options that your database passes directly to the wsrep provider.

- ``wsrep_cluster_name=example_cluster``
  
  This sets the logical cluster name.  If a node tries to connect to the cluster with a different cluster name, the connection fails.

- ``wsrep_node_address='192.168.0.2'``

  This explicitly sets the network address of the node, for use in the event that auto-guessing does not produce desirable results.

- ``wsrep_node_name='example_node2'``
  
  This sets the logical node name for convenience.

- ``wsrep_sst_method=xtrabackup``
  
  This sets the method to use for State Snapshot Transfers (SST).

- ``wsrep_sst_auth=root:rootpa$$``
  
  This sets a string with authentication information for State Snapshot Transfers (SST).


------------------------------------
Optional wsrep Provider Settings
------------------------------------
.. _`Optional wsrep Provider Settings`:

For better performance, consider using the following wsrep provider settings:

- ``wsrep_node_incoming_address='192.168.10.2'``
  
  This sets the address from which the server expects client connections, for use in integration with load balancers.

- ``wsrep_sst_donor=example_node3``
  
  This sets the logical name of the server that the node should use as a source in State Snapshot Transfers (SST).  The donor name is the same as the ``wsrep_node_name`` parameter used for the source node.

- ``wsrep_slave_threads=16``
  
  This sets how many threads to use for applying slave write-sets.


----------------------------------
Optional Memory Settings
----------------------------------
.. _`Optional Memory Settings`:

In normal operation, a Galera Cluster node does not consume much more memory than a regular database server.  Certification indexes and uncommitted write-sets do consume more memory, but usually this is not noticeable in typical applications.  Write-set caching during state transfers is the exception.

When a node receives a state transfer, it cannot process and apply incoming write-sets, because it does not have a state yet to apply them to.  Depending on the state transfer mechanism, (for example, ``mysqldump``), the node that sends the state transfer may also not be able to apply write-sets.  Instead, the node must cache the write-sets for a catch-up phase.

The write-set cache, (GCache), is used to cache write-sets on memory-mapped files to disk and allocate them as needed.  In other words, the limit for the cache is the available disk space.  Writing on disk reduces memory consumption.

To adjust the flow control settings, you can use the parameters below:

- ``gcs.recv_q_hard_limit``
  
  This sets the maximum allowed size of the recv queue.  Set the value to about half the available memory, including swap.

  If your server exceeds this limit, Galera Cluster aborts the server.

- ``gcs.recv_q_soft_limit``
  
  This sets the throttle point for your server.  The value must be lower than the hard limit.

  When the recv queue reaches this point, Galera Cluster begins to throttle the replication rate to prevent it from reaching the maximum allowed limit.

- ``gcs.max_throttle``

  This sets how much Galera Cluster can throttle the replication rate during state transfers, to avoid running out of memory.


