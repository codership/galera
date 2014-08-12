========================
 Configuration Tips
========================
.. _`Configuration Tips`:

This chapter contains some advanced configuration tips.

-------------------
 WAN Replication
-------------------
.. _`WAN Replication`:

.. index::
   pair: Configuration Tips; wsrep_provider_options
.. index::
   single: my.cnf

Transient network connectivity failures are not rare in :abbr:`WAN (Wide Area Network)` configurations. Thus, you may want to increase the keepalive timeouts to avoid partitioning. The following group of ``my.cnf`` settings tolerates 30 second connectivity outages:

.. code-block:: ini

  wsrep_provider_options = "evs.keepalive_period = PT3S; 
  	evs.inactive_check_period = PT10S; 
  	evs.suspect_timeout = PT30S; 
  	evs.inactive_timeout = PT1M; 
  	evs.install_timeout = PT1M"

Set the ``evs.suspect_timeout`` parameter value as high as possible to avoid partitions (as partitions will cause state transfers, which are very heavy). The ``evs.inactive_timeout`` parameter value must be no less than the ``evs.suspect_timeout`` parameter value and the ``evs.install_timeout`` parameter value must be no less than the ``evs.inactive_timeout`` parameter value.

.. note:: WAN links can have exceptionally high latencies. Take Round-Trip Time (RTT) measurements (ping RTT is a fair estimate) from between your cluster nodes and make sure that all temporal Galera Cluster settings (periods and timeouts, such as ``evs.join_retrans_period``) exceed the highest RTT in your cluster.
  
---------------------
 Multi-Master Setup
---------------------
.. _`Multi-Master Setup`:

The more masters (nodes which simultaneously process writes from clients) are in the cluster, the higher the probability of certification conflict. This may cause undesirable rollbacks and performance degradation.  In such a case, reduce the number of nodes used as masters.

----------------------
 Single Master Setup
----------------------
.. _`Single Master Setup`:

.. index::
   pair: Configuration Tips; wsrep_provider_options

If only one node at a time is used as a master, certain requirements, such as the slave queue size, may be relaxed. Flow control can be relaxed by using the settings below:

.. code-block:: ini

    wsrep_provider_options = "gcs.fc_limit = 256; 
    	gcs.fc_factor = 0.99; 
    	gcs.fc_master_slave = YES"

These settings may improve replication performance by reducing the rate of flow control events. This setting can also be used as suboptimal in a multi-master setup.



------------------------------------
 Using Galera Cluster with SElinux
------------------------------------
.. _`Using Galera Cluster with SElinux`:

.. index::
   pair: Configuration; SELinux

If you want to use Galera Cluster with SElinux, start by running SELinux in the permissive mode. In this mode, SELinux will not prohibit any Galera Cluster actions, but will log a warning for all actions that would have been prohibited. Collect these warnings and iteratively create a policy for *Galera Cluster* that allows to use all the different ports and files that you need. When there are no more warnings, switch back to the enforcing mode. 

Virtually every Linux distribution ships with a MySQL SELinux policy. You can use this policy as a starting point and extend it with the above procedure.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
