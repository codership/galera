==========================
 Schema Upgrades
==========================
.. _`Schema Upgrades`:

A schema upgrade refers to any :abbr:`DDL (Data Definition Language)`
statement run for the database. :abbr:`DDL (Data Definition Language)`
statements change the database structure and are non-transactional.

:abbr:`DDL (Data Definition Language)` statements are processed in
two different methods in *Galera Cluster*.
These methods are described in the chapters below.

.. note:: See also the ``pt-online-schema-change`` command in Percona
          Toolkit for MySQL: http://www.percona.com/software/percona-toolkit.

---------------------------------
 Total Order Isolation
---------------------------------
.. _`Total Order Isolation`:
.. index::
   pair: Descriptions; Total Order Isolation

By default, :abbr:`DDL (Data Definition Language)`
statements are processed by using the Total Order Isolation
(TOI) method. In TOI, the query is replicated to the nodes in a statement
form before executing on master. The query waits for all preceding transactions
to commit and then gets executed in isolation on all nodes simultaneously.
When using the TOI method, the cluster has a part of the database locked for
the duration of the :abbr:`DDL (Data Definition Language)`
processing (in other words, the cluster behaves like
a single server).

The isolation can take place at the following levels:

1. At the server level, where no other transactions can be
   applied concurrently (for ``CREATE SCHEMA``, ``GRANT`` and
   similar queries).
2. At the schema level, where no transaction accessing the
   schema can be applied concurrently (for ``CREATE TABLE``
   and similar queries).
3. At the table level, where no transaction accessing the
   table can be applied concurrently (for ``ALTER TABLE``
   and similar queries).

TOI queries have several particularities  that must been taken
into consideration:

- From the perspective of certification:

  - TOI transactions never conflict with preceding transactions,
    since they are only executed after all preceding transactions
    were committed. Hence, their certification interval is of zero
    length. This means that TOI transactions will never fail
    certification and are guaranteed to be executed.
  - Certification takes place on a resource level. For example,
    for server-level isolation this means any transaction that
    has a TOI query in its certification interval will fail
    certification.

- The system replicates the TOI query before execution and there
  is no way to know whether it succeeds or fails. Thus, error checking
  on TOI queries is switched off.
- The method is simple, predictable and guarantees data consistency.
- The disadvantage is that the cluster behaves like a single server,
  potentially preventing high-availability for the duration of
  :abbr:`DDL (Data Definition Language)` execution.

---------------------------------
 Rolling Schema Upgrade
---------------------------------
.. _`Rolling Schema Upgrade`:
.. index::
   pair: Descriptions; Rolling Schema Upgrade
.. index::
   pair: Parameters; wsrep_OSU_method

As of *wsrep* patch 5.5.17-22.3, you can choose whether to use the
traditional total order isolation method or the rolling schema upgrade
method. You can choose the rolling schema upgrade method by using the
global parameter ``wsrep_OSU_method``.

The rolling schema upgrade is a :abbr:`DDL (Data Definition Language)`
processing method, where the :abbr:`DDL (Data Definition Language)`
will only be processed locally at the node. The node is desynchronized
from the cluster for the duration of the :abbr:`DDL (Data Definition Language)`
processing in a way that it does not block the rest of the nodes.
When the :abbr:`DDL (Data Definition Language)` processing is complete,
the node applies the delayed replication events and synchronizes back
with the cluster.

To upgrade a schema cluster-wide, the :abbr:`DDL (Data Definition Language)`
must be manually executed at each node in turn. When the rolling schema
upgrade proceeds, a part of the cluster will have the old schema structure
and a part of the cluster will have the new schema structure.

.. warning:: While the rolling schema upgrade has the advantage of
             blocking only one node at a time, it is potentially unsafe,
             and may fail if the new and old schema definitions are
             incompatible at the replication event level. Execute
             operations such as ``CREATE ATBLE`` and ``DROP TABLE``
             in TOI.
