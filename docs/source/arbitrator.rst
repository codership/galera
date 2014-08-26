===================
 Galera Arbitrator
===================
.. _`arbitrator`:

.. index::
   pair: Descriptions; Galera Arbitrator
.. index::
   single: Split-brain; Prevention
.. index::
   pair: Logs; Galera Arbitrator

The recommended deployment of Galera Cluster is that you use a minimum of three instances.  Three nodes, three datacenters and so on.

In the event that the expense of adding resources, such as a third datacenter, is too costly, you can use the Galera Arbitrator.  **garbd** is a member of the cluster that participates in voting, but not in the actual replication.

.. warning:: While the Galera Arbitrator does not participate in replication, it does receive the same data as all other nodes.  You must secure its network connection.

The Galera Arbitrator serves two purposes:

- In the event that the cluster spreads only over two nodes, it functions as an odd node.

- It can request a consistent application state snapshot.

Galera Arbitrator is depicted in the figure below:

.. figure:: images/arbitrator.png

   *Galera Arbitrator*

If one datacenter fails or loses :abbr:`WAN (Wide Area Network)` connection, the node that sees the arbitrator, and by extension sees clients, continues operation.

.. note:: Even though the Galera Arbitrator does not store data, it must see all replication traffic.  Placing **garbd** in a location with poor network connectivity to the rest of the cluster may lead to poor cluster performance.

In the event that **garbd** fails, it does not affect cluster operation.  You can attach a new instance to the cluster at any time and there can be several instances running in the cluster.



--------------------------------
 Configuring Galera Arbitrator
--------------------------------
.. _`arbitrator-configuration`:
.. index::
   pair: Configuration; Galera Arbitrator

The Galera Arbitrator functions as a member of the cluster.  Any configuration parameters that you can use with a given node you can also apply to the arbitrator, with the exception of those parameters prefixed by ``replicator``.

.. seealso:: For more information of the configuration parameters available to Galera Arbitrator, see :doc:`Galera Parameters <galeraparameters>`.



----------------------------
 Starting Galera Arbitrator
----------------------------
.. _`starting-arbitrator`:

The Galera Arbitrator is a separate daemon called **garbd**.

To manually start the Galera Arbitrator, run the following command:

.. code-block:: console

    $ /etc/init.d/garb start 

The **garbd** command can receive configuration options for settings you want to enable at start.

Additionally, you can automate running the Galera Arbitrator using an init script:

.. code-block:: linux-config

   # Copyright (C) 2013 Codership Oy
   # This config file is to be sourced by garbd service script.
   
   # A space-separated list of node addresses (address[:port]) in the cluster:
   GALERA_NODES="192.168.1.1:4567 192.168.1.2:4567"

   # Galera cluster name, should be the same as on the rest of the node.
   GALERA_GROUP="example_wsrep_cluster"

   # Optional Galera internal options string (e.g. SSL settings)
   # see http://www.codership.com/wiki/doku.php?id=galera_parameters
   GALERA_OPTIONS="socket.ssl_cert = /etc/galera/cert/cert.pem; socket.ssl_key = /$"
    
   # Log file for garbd. Optional, by default logs to syslog
   LOG_FILE="/var/log/garbd.log"

