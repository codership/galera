======================================
 Configuring Galera Cluster for MySQL
======================================
.. _`Configuring Galera Cluster for MySQL`:

.. index::
   pair: Parameters; wsrep_cluster_address
.. index::
   pair: Parameters; wsrep_provider

This chapter presents the mandatory and recommended settings
for *Galera Cluster* installation and use. It may
be possible to start the cluster after
only setting the ``wsrep_provider`` and ``wsrep_cluster_address``
variables. However, the best results can be achieved by
fine-tuning the configuration to best match the use case.

.. seealso:: Chapter :ref:`Galera Parameters <Galera Parameters>`.

----------------------------
 Installation Configuration
----------------------------
.. _`Installation Configuration`:

Unless you are upgrading an already installed *mysql-wsrep*
package, you must configure the installation to prepare the
server for operation.


Configuration Files
====================
.. _`Configuration Files`:

.. index::
   pair: Configuration files; wsrep.cnf
.. index::
   pair: Configuration files; my.cnf

Edit the *my.cnf* configuration file as follows:

- Make sure that the system-wide *my.cnf* file does not bind *mysqld*
  to 127.0.0.1. To be more specific, if you have the following line
  in the [mysqld] section, comment it out::

      #bind-address = 127.0.0.1

- Make sure that the system-wide *my.cnf* file contains the line below::
  
    !includedir /etc/mysql/conf.d/

Edit the */etc/mysql/conf.d/wsrep.cnf* configuration file as follows:

- When a new node joins the cluster, it will have to receive a state
  snapshot from one of the peers. This requires a privileged MySQL
  account with access from the rest of the cluster. Set the *mysql*
  login/password pair for SST in the */etc/mysql/conf.d/wsrep.cnf*
  configuration file as follows::

      wsrep_sst_auth=wsrep_sst:wspass

Database Privileges
====================

Restart the MySQL server and connect to it as root to grant privileges
to the SST account. Furthermore, empty users confuse MySQL authentication
matching rules. Delete them::

    $ mysql -e "SET wsrep_on=OFF; DELETE FROM mysql.user WHERE user='';"
    $ mysql -e "SET wsrep_on=OFF; GRANT ALL ON *.* TO wsrep_sst@'%' IDENTIFIED BY 'wspass'";


Firewall Settings
====================

.. index::
   pair: Configuration; Firewall

The *MySQL-wsrep* server must be accessible from other cluster members through
its client listening socket and through the wsrep provider socket. See your
distribution and wsrep provider documentation for details. For example, on
CentOS you could use these settings::

    # iptables --insert RH-Firewall-1-INPUT 1 --proto tcp --source <my IP>/24 --destination <my IP>/32 --dport 3306 -j ACCEPT
    # iptables --insert RH-Firewall-1-INPUT 1 --proto tcp --source <my IP>/24 --destination <my IP>/32 --dport 4567 -j ACCEPT
    # iptables --insert RH-Firewall-1-INPUT 1 --proto tcp --source <my IP>/24 --destination <my IP>/32 --dport 4568 -j ACCEPT

If there is a NAT firewall between the nodes, configure it to allow
direct connections between the nodes (for example, through port forwarding).


SELinux
====================

.. index::
   pair: Configuration; SELinux

If you have SELinux enabled, it may block *mysqld* from carrying out the
required operations. Disable SELinux or configure it to allow *mysqld*
to run external programs and open listen sockets at unprivileged ports
(that is, things that an unprivileged user can do). See SELinux
documentation for more information.

To disable SELinux, proceed as follows:

1) run *setenforce 0* as root.
2) set ``SELINUX=permissive`` in  */etc/selinux/config*


AppArmor
====================

.. index::
   pair: Configuration; AppArmor

AppArmor is always included in Ubuntu. It may prevent *mysqld* from
opening additional ports or run scripts. See AppArmor documentation
for more information on its configuration.

To disable AppArmor, proceed as follows::

    $ cd /etc/apparmor.d/disable/
    $ sudo ln -s /etc/apparmor.d/usr.sbin.mysqld
    $ sudo service apparmor restart


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
- ``innodb_file_per_table`` |---| When ``innodb_file_per_table`` is enabled,
  InnoDB stores the data and indexes for each newly created table in
  a separate *.ibd* file, rather than in the system tablespace. 
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
  
  .. warning:: With ``innodb_flush_log_at_trx_commit=2``, some transactions
               can be lost if the entire cluster goes down, for example, due
               to a datacenter power outage. 

  Set::

    ``innodb_flush_log_at_trx_commit=2``


---------------------------
 wsrep Provider Settings
---------------------------
.. _`wsrep Provider Settings`:

The basic wsrep provider settings are:

- ``wsrep_provider=/usr/lib64/galera/libgalera_smm.so`` |---| The
  path to the Galera Replication Plugin.
- ``wsrep_cluster_address=gcomm://192.168.0.1,192.168.0.2,192.168.0.3`` |---| The
  cluster connection URL. See chapter :ref:`Starting a Cluster <Starting a Cluster>`.
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
  be used as a source for state transfer. Give the donor node name as
  configured with the ``wsrep_node_name`` parameter on the desired donor.
- ``wsrep_slave_threads=16`` |---| How many threads to use for applying
  slave writsets.

---------------------------
 Optional Memory Settings
---------------------------
.. _`Optional Memory Settings`:

.. index::
   pair: Performance; Memory
.. index::
   pair: Performance; Swap size

In normal operation, a *Galera Cluster* node does not consume
much more memory than a regular MySQL server. Additional
memory is consumed for the certification index and uncommitted
write sets, but usually this is not noticeable in a typical
application. However, writeset caching during state transfer
makes an exception.

When a node is receiving a state transfer, it cannot process
and apply incoming write sets because it has no state to
apply them to yet. Depending on the state transfer mechanism
(for example, *mysqldump*), the node that sends the state
transfer may not be able to apply write sets. Instead, the
node must cache the write sets for a catch-up phase. The
Writeset Cache (GCache) is used to cache write sets on
memory-mapped files on disk. These files are allocated as
needed. In other words, the limit for the cache is the
available disk space. Writing on disk reduces memory
consumption.

However, if you want to adjust flow control settings, adjust the
*Galera Cluster* parameters below:

- ``gcs.recv_q_hard_limit`` |---| the maximum allowed size of
  recv queue. This should normally be half of (RAM + swap).
  If this limit is exceeded, *Galera Cluster* will abort the server
- ``gcs.recv_q_soft_limit`` |---| A fraction of ``gcs.recv_q_hard_limit``
  after which replication rate will be throttled.
- ``gcs.max_throttle`` |---| How much we can throttle the replication
  rate during state transfer (to avoid running out of memory).

.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
