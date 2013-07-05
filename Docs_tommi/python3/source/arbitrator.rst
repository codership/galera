===================
 Galera Arbitrator
===================
.. _`Galera Arbitrator`:

.. index::
   pair: Descriptions; Galera Arbitrator
.. index::
   single: Split-brain; Prevention
.. index::
   pair: Logs; Galera Arbitrator

If the expense of adding, for example, a third datacenter is too high,
you can use Galera Arbitrator. Galera Arbitrator is a member of the
cluster which participates in voting, but not in actual replication.

.. warning:: Galera Arbitrator does not participate in actual replication,
             but it receives the same data as the nodes. Use a secure
             network connection for the arbitrator.

Galera Arbitrator servers two purposes:

- It helps to avoid split-brain situations by acting as an odd
  node in a cluster that is spread only across two nodes.
- It can request a consistent application state snapshot.

Galera Arbitrator is depicted in the figure below:

.. figure:: images/arbitrator.png

   *Galera Arbitrator*

In the figure above, if one of the data centers fails or loses
WAN connection, the node that sees the arbitrator (and therefore
sees clients) will continue operation.

.. note:: *garbd* must see all replication traffic although it does not
          store it anywhere. Placing the arbitrator in a location with
          poor connectivity to the rest of the cluster may lead to poor
          cluster performance.

Arbitrator failure does not affect cluster operation and a new
instance can be reattached to the cluster at any time. There can be
several arbitrators in the cluster.


--------------------------------
 Configuring Galera Arbitrator
--------------------------------
.. _`Configuring Galera Arbitrator`:
.. index::
   pair: Configuration; Galera Arbitrator

As a *Galera Cluster* cluster member,
Galera Arbitrator accepts all *Galera Cluster*
parameters except those prefixed by ``replicator.``.

.. seealso:: Chapters :ref:`Galera Parameters <Galera Parameters>`
             and :ref:`Starting Galera Arbitrator <Starting Galera Arbitrator>`.

----------------------------
 Starting Galera Arbitrator
----------------------------
.. _`Starting Galera Arbitrator`:

Galera Arbitrator it is a separate daemon called *garbd*. 
You can start is manually as follows::

    # /etc/init.d/garb start 

You can also add configuration options to the command.

You can also automate running Galera Arbitrator by using an
*/etc/default/garb* init script, such as the one below::

    # Copyright (C) 2013 Codership Oy
    # This config file is to be sourced by garb service script.
    
    # A space-separated list of node addresses (address[:port]) in the cluster
    GALERA_NODES="192.168.1.1:4567 192.168.1.2:4567"
    
    # Galera cluster name, should be the same as on the rest of the nodes.
    GALERA_GROUP="example_wsrep_cluster"
    
    # Optional Galera internal options string (e.g. SSL settings)
    # see http://www.codership.com/wiki/doku.php?id=galera_parameters
    GALERA_OPTIONS="socket.ssl_cert = /etc/galera/cert/cert.pem; socket.ssl_key = /$
    
    # Log file for garbd. Optional, by default logs to syslog
    LOG_FILE="/var/log/garbd.log"

