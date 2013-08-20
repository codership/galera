====================== 
 Isolation Levels
======================
.. _`Isolation Levels`:

Isolation guarantees that transactions are processed in a
reliable manner. To be more specific, it ensures that concurrently
running transactions do not interfere with each other. In this way,
isolation ensures data consistency. If the transactions were not
isolated, one transaction could modify data that other transactions
are reading. This would lead to data inconsistency.

The four isolation levels are, from lowest to highest:

- ``READ-UNCOMMITTED`` |---| On this isolation level, transactions can
  see changes to data made by other transactions that are not committed
  yet. In other words, transactions can read data that may not eventually
  exist, as the other transactions can always roll-back the changes
  without commit. This is called a *dirty read*. ``READ-UNCOMMITTED``
  has actually no real isolation at all.
- ``READ-COMMITTED`` |---| On this isolation level, dirty reads are
  impossible, as uncommitted changes are invisible to other transactions
  until the transaction is committed. However, at this isolation level,
  ``SELECT`` clauses use their own snapshots of committed data, committed
  before the ``SELECT`` clause was executed. As a result, the same
  ``SELECT`` clause, when run multiple times within the same transaction,
  can return different result sets. This is called a *non-repeatable read*.
- ``REPEATABLE-READ`` |---| On this isolation level, non-repeatable reads
  are impossible, as the snapshot for the ``SELECT`` clause is taken the
  first time the ``SELECT`` clause is executed during the transaction.
  This snapshot is used throughout the entire transaction for this
  ``SELECT`` clause and it always returns the same result set. This level
  does not take into account changes to data made by other transactions,
  regardless of whether they have been committed or not. In this way,
  reads are always repeatable.
- ``SERIALIZABLE`` |---| This isolation level place locks on all records
  that are accessed within a transaction. ``SERIALIZABLE`` also locks
  the resource in a way that records cannot be appended to the table being
  operated on by the transaction. This level prevents a phenomenon known
  as a *phantom read*. A phantom read occurs when, within a transaction,
  two identical queries are executed, and the rows returned by the second
  query are different from the first.

*Galera Cluster* uses transaction isolation on two levels:

- Locally, that is, on each node, transaction isolation works as
  with native InnoDB. You can use all levels. The default isolation
  level for InnoDB is ``REPEATABLE-READ``. 
- At the cluster level, between transactions processing at separate
  nodes, *Galera Cluster* implements a transaction level called ``SNAPSHOT ISOLATION``.
  The ``SNAPSHOT ISOLATION`` level is between the ``REPEATABLE READ``
  and ``SERIALIZABLE`` levels.

  The ``SERIALIZABLE`` transaction isolation level is not supported
  in a multi-master use case, , not in the ``STATEMENT`` nor in the
  ``ROW`` format. This is due to the fact that Galera replication
  does not carry a transaction read set. Also, the ``SERIALIZABLE``
  transaction isolation level is vulnerable for multi-master
  conflicts. It holds read locks and any replicated write to a
  read locked row will cause the transaction to abort. Hence,
  it is recommended not to use it in *Galera Cluster*.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
