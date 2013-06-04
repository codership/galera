========================
 Configuration Tips
========================
.. _`Configuration Tips`:

This chapter contains some advanced configuration tips.

--------------------------------------
Setting Parallel CPU Threads
--------------------------------------
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
 
-------------------
 WAN Replication
-------------------
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
  
---------------------
  Multi-Master Setup
---------------------
.. _`Multi-Master Setup`:

The more masters (nodes which simultaneously process writes from
clients) are in the cluster, the higher the probability of certification
conflict. This may cause undesirable rollbacks and performance degradation.
In such a case, reduce the number of masters.

----------------------
  Single Master Setup
----------------------
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