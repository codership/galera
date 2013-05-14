====================
 Galera Replication
====================
.. _`Galera Replication`:

Galera Replication is a synchronous multi-master replication
plug-in for MySQL and MariaDB. Galera Replication features are,
for example:

- Unconstrained parallel applying, also known as parallel
  replication
- Multicast replication
- Automatic node provisioning

The primary focus of Galera Replication is data consistency:
the transactions are either applied on every node, or not at
all. In short, Galera Replication keeps databases synchronized
provided that they were properly configured and synchronized
at the beginning.

Galera Replication differs from the regular MySQL Replication
by, for example, addressing a number of issues including write
conflicts when writing on multiple masters, replication lag
and slaves being out of sync with the master.

See below for an example of a typical Galera Replication
cluster:

.. figure:: images/galerausecases1.png

   *Galera Replication Cluster*

Applications can write to any node in a Galera Replication
cluster, and transaction commits (RBR events) are then
applied on all servers, through certification-based replication.

Certification-based replication is an alternative approach to
synchronous database replication using group communication
and transaction ordering techniques.

A minimal Galera cluster consists of three nodes.
