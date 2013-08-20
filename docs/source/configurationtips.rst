========================
 Configuration Tips
========================
.. _`Configuration Tips`:

This chapter contains some advanced configuration tips.

--------------------------------------
 Setting Parallel Slave Threads
--------------------------------------
.. _`Setting Parallel Slave Threads`:

.. index::
   pair: Configuration Tips; innodb_autoinc_lock_mode
.. index::
   pair: Configuration Tips; innodb_locks_unsafe_for_binlog
.. index::
   pair: Configuration Tips; wsrep_slave_threads

There is no rule about how many slave threads one should
configure for replication. At the same time, parallel threads
do not guarantee better performance. However, parallel
applying will not impair regular operation performance and
will most likely speed up the synchronization of new nodes
with the cluster.

Start with four slave threads per CPU core, the logic being that, in a
balanced system, four slave threads can usually saturate the core.
However, depending on IO performance, this figure can be increased
several times (for example, you can use 32 slave threads on a
single-core ThinkPad R51 with a 4200 RPM drive). 

The top limit on the total number of slave threads can be
obtained from the ``wsrep_cert_deps_distance`` status
variable. This value essentially determines how many writesets
on average can be applied in parallel. Do not use a value higher
than that.

To set four parallel slave threads, use the parameter value below::

    wsrep_slave_threads=4

.. note:: Parallel applying requires the following settings:

          - ``innodb_autoinc_lock_mode=2``
          - ``innodb_locks_unsafe_for_binlog=1``
 
-------------------
 WAN Replication
-------------------
.. _`WAN Replication`:

.. index::
   pair: Configuration Tips; wsrep_provider_options
.. index::
   single: my.cnf

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

.. note:: WAN links can have exceptionally high latencies. Take
          Round-Trip Time (RTT) measurements (ping RTT is a fair estimate)
          from between your cluster nodes and make sure
          that all temporal *Galera Cluster*
          settings (periods and timeouts, such
          as ``evs.join_retrans_period``) exceed the highest RTT in
          your cluster.
  
---------------------
 Multi-Master Setup
---------------------
.. _`Multi-Master Setup`:

The more masters (nodes which simultaneously process writes from
clients) are in the cluster, the higher the probability of certification
conflict. This may cause undesirable rollbacks and performance degradation.
In such a case, reduce the number of nodes used as masters.

----------------------
 Single Master Setup
----------------------
.. _`Single Master Setup`:

.. index::
   pair: Configuration Tips; wsrep_provider_options

If only one node at a time is used as a master, certain requirements,
such as the slave queue size, may be relaxed. Flow control can be
relaxed by using the settings below::

    wsrep_provider_options = "gcs.fc_limit = 256; gcs.fc_factor = 0.99; gcs.fc_master_slave = yes"

These settings may improve replication performance by
reducing the rate of flow control events. This setting
can also be used as suboptimal in a multi-master setup.

--------------------------
 Customizing GCache Size
--------------------------
.. _`Customizing GCache Size`:

.. index::
   pair: Configuration Tips; gcache.size

.. index::
   pair: Configuration Tips; wsrep_received_bytes

These configuration tips are guidelines only. You may end up
using a bigger GCache than suggested by these guidelies, for
example, if you must avoid SST as much as possible. 

The GCache size, that is, the ``gcache.size`` parameter value,
should be smaller than the database size. However, in this context,
the database size depends on the SST method. For example,
*mysqldump* does not copy InnoDB log files whereas *rsync*
and *xtrabackup* do. As a rule, it is recommended to use the
data directory size (including any possible links) minus the
size of the ``galera.cache`` parameter.

You can also consider the speed of copying as one variable in
the calculation. If you use Incremental State Transfer (IST)
as your node provisioning method, you can probably copy the
database five times faster through IST than through *mysqldump*.
With *xtrabackup*, the factor is approximately 1.5. If this
is the case, you can use a relatively big GCache size.

The database write rate indicates the tail length that will be
stored in the GCache. You can calculate the write rate by using
the ``wsrep_received_bytes`` status variable. Proceed as follows:

1. Read the ``wsrep_received_bytes1`` value at time ``t1``.
2. Read the ``wsrep_received_bytes2`` value at time ``t2``.
3. Calculate the write rate as follows:

   ``(wsrep_received_bytes2 - wsrep_received_bytes1)/(t2 - t1)``

------------------------------------
 Using Galera Cluster with SElinux
------------------------------------
.. _`Using Galera Cluster with SElinux`:

.. index::
   pair: Configuration; SELinux

If you want to use *Galera Cluster* with SElinux, start by running SELinux
in the permissive mode. In this mode, SELinux will not prohibit
any *Galera Cluster* actions, but will log a warning for all actions that
would have been prohibited. Collect these warnings and iteratively 
create a policy for *Galera Cluster* that allows to use all the different
ports and files that you need. When there are no more warnings,
switch back to the enforcing mode. 

Virtually every Linux distribution ships with a MySQL SELinux
policy. You can use this policy as a starting point and extend
it with the above procedure.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
