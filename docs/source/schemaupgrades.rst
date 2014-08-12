==========================
 Schema Upgrades
==========================
.. _`Schema Upgrades`:

Any :abbr:`DDL (Data Definition Language)` statement that runs for the database, such as ``CREATE TABLE`` or ``GRANT``, upgrades the schema.  These :abbr:`DDL (Data Definition Language)` statements change the database itself and are non-transactional.

Galera Cluster processes schema upgrades in two different methods:

- :ref:`Total Order Isolation (TOI) <toi>` Where the schema upgrades run on all cluster nodes in the same total order sequence, locking affected tables for the duration of the operation.

- :ref:`Rolling Schema Upgrade (RSU) <rsu>` Where the schema upgrades run locally, blocking only the node on which they are run.  The changes do *not* replicate to the rest of the cluster.

You can set the method for online schema upgrades by using the ``wsrep_OSU_method`` parameter in the configuration file, (**my.ini** or **my.cnf**, depending on your build) or through the MySQL client.  Galera Cluster defaults to the Total Order Isolation method.

.. seealso:: If you are using Galera Cluster for Percona XtraDB Cluster, see the the `pt-online-schema-change <http://www.percona.com/doc/percona-toolkit/2.2/pt-online-schema-change.html>`_ in the Percona Toolkit.




---------------------------------
 Total Order Isolation
---------------------------------
.. _`toi`:
.. index::
   pair: Descriptions; Total Order Isolation

When you want your online schema upgrades to replicate through the cluster and don't mind the loss of high availability while the cluster processes the :abbr:`DDL (Data Definition Language)` statements, use the Total Order Isolation method.

.. code-block:: mysql

   SET GLOBAL wsrep_OSU_method='TOI';

In Total Order Isolation, queries that update the schema replicate as statements to all nodes in the cluster before they execute on the master.  The nodes wait for all preceding transactions to commit then, simultaneously, they execute the schema upgrade in isolation.  For the duration of the :abbr:`DDL (Data Definition Language)` processing, part of the database remains locked, causing the cluster to function as single server.

The cluster can maintain isolation at the following levels:

- **Server Level** For ``CREATE SCHEMA``, ``GRANT`` and similar queries, where the cluster cannot apply concurrently any other transactions.

- **Schema Level** For ``CREATE TABLE`` and similar queries, where the cluster cannot apply concurrently any transactions that access the schema.

- **Table Level** For ``ALTER TABLE`` and similar queries, where the cluster cannot apply concurrently any other transactions that access the table.

The main advantage of Total Order Isolation is its simplicity and predictability, which guarantees data consistency.

In addition, when using Total Order Isolation you should take the following particularities into consideration:

- From the perspective of certification, schema upgrades in Total Order Isolation never conflict with preceding transactions, given that they only execute after the cluster commits all preceding transactions.  What this means is that the certification interval for schema upgrades in this method is of zero length.  The schema upgrades never fail certification and their execution is a guarantee.

- The certification process takes place at a resource level.  Under server-level isolation transactions that come in during the certification interval that include schema upgrades in Total Order Isolation, will fail certification.

- The cluster replicates the schema upgrade query as a statement before its execution.  There is no way to know whether or not the nodes succeed in processing the query.  This prevents error checking on schema upgrades in Total Order Isolation.

The main disadvantage of Total Order Isolation is that while the nodes process the :abbr:`DDL (Data Definition Language)` statements, the cluster functions as a single server, which can potentially prevent high-availability for the duration of the process.


---------------------------------
 Rolling Schema Upgrade
---------------------------------
.. _`rsu`:
.. index::
   pair: Descriptions; Rolling Schema Upgrade
.. index::
   pair: Parameters; wsrep_OSU_method

When you want to maintain high-availability during schema upgrades and can avoid conflicts between new and old schema definitions, use the Rolling Schema Upgrade method.

.. code-block:: mysql

   SET GLOBAL wsrep_OSU_method='RSU';

In Rolling Schema Upgrade, queries that update the schema are only processed on the local node.  While the node processes the schema upgrade, it desynchronizes with the cluster.  When it finishes processing the schema upgrade it applies delayed replication events and synchronizes itself with the cluster.

To upgrade the schema cluster-wide, you must manually execute the query on each node in turn.  Bear in mind that during a rolling schema upgrade the cluster continues to operate, with some nodes using the old schema structure while others use the new schema structure. 

The main advantage of the Rolling Schema Upgrade is that it only blocks one node at a time.

The main disadvantage of the Rolling Schema Upgrade is that it is potentially unsafe, and may fail if the new and old schema definitions are incompatible at the replication event level.

.. warning:: To avoid conflicts between new and old schema definitions, execute operations such as ``CREATE TABLE`` and ``DROP TABLE`` using the :ref:`Total Order Isolation <toi>` method.


