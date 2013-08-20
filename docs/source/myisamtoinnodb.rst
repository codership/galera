=========================================================
 Migrating from MyISAM to Galera Cluster for MySQL
=========================================================
.. _`Migrating from MyISAM to Galera Cluster for MySQL`:

.. index::
   pair: Installation; Migrating from MyISAM

These instructions describe how to migrate from the MyISAM
storage engine to the InnoBD storage engine on
*Galera Cluster*. These instructions
are applicable to both
a standalone MySQL server and a stock MySQL master-slave
cluster that uses the MyISAM storage engine.

Proceed as follows:

1. Create a Galera Cluster cluster. The
   cluster can consist of one node only, if necessary.
2. Stop all load on the MyISAM master.
3. Initialize the *Galera Cluster*
   by performing a ``mysqldump``
   with ``--skip-create-options``. After this operation, the
   database will by default create InnoDB tables on the cluster.
4. Resume the load on one of the cluster nodes.
5. Upgrade the *mysqld* on the former master to *Galera Cluster* software.
6. Convert the tables to the InnoDB format on the former master
   node.
7. Copy the **grastate.dat** file from one of the cluster nodes
   to the former master.
8. Open the **grastate.dat** file and change the seqno from
   -1 to 0 there.
9. Join the former master to the *Galera Cluster*.

The downtime for the migration is the time it takes to perform step 3.
