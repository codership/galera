====================================================
 Differences to a Standalone MySQL Server
====================================================
.. _`Differences to a Standalone MySQL Server`:

Galera has the following differences to a standalone MySQL server:

- Replication only works with the InnoDB storage engine. Any writes to tables
  of other types, including system (mysql.*) tables are not replicated. However,
  DDL statements are replicated at the statement level, and changes to mysql.*
  tables will get replicated that way. In other words, you can safely issue
  command ``CREATE USER...`` or ``GRANT...``, but issuing ``INSERT INTO mysql.user...``
  will not be replicated. 
  
  In general, non-transactional engines cannot be supported in multi-master replication.
- Rows in tables without primary key may appear in different order on different
  nodes. As a result, command ``SELECT...LIMIT...`` may return slightly different
  sets. The ``DELETE`` operation is also unsupported on tables without primary key.

  Do not use tables without a primary key. It is always possible to add an
  ``AUTO_INCREMENT`` column to a table without breaking your application.
- Unsupported queries:

    - ``LOCK``/``UNLOCK TABLES`` cannot be supported in master-master replication.
    - Lock functions (``GET_LOCK()``, ``RELEASE_LOCK()...``)

- Query logs cannot be directed to a table. If you enable query logging, you must
  forward the log to a file::
  
    log_output = FILE

  Use ``general_log`` and ``general_log_file`` to choose query logging and log file name.
- Do not use a query cache.
- XA transactions cannot be supported due to possible rollback on commit.
- Transaction size. While Galera does not explicitly limit the transaction size,
  a writeset is processed as a single memory-resident buffer and, as a result,
  extremely large transactions (for example, ``LOAD DATA``) may adversely affect
  node performance. To avoid that, the ``wsrep_max_ws_rows`` and ``wsrep_max_ws_size``
  variables limit the transaction rows to 128K and the transaction size to 1Gb,
  by default. If necessary, you can increase those limits.
- Due to cluster level optimistic concurrency control, a transaction issuing
  a ``COMMIT`` may still be aborted at that stage. There can be two transactions
  writing the to same rows and committing in separate cluster nodes, and only one
  of the them can successfully commit. The failing one will be aborted.
  
  For cluster level aborts, *Galera Cluster for MySQL* gives back a deadlock error::
  
     code (Error: 1213 SQLSTATE: 40001  (ER_LOCK_DEADLOCK))

- Windows is not supported.
- Do not use ``binlog-do-db`` and ``binlog-ignore-db``. These binary log
  options are only supported for DML, but not for DDL. Using these options
  will create a discrepancy and replication will abort.
- Do not use server system variables ``character_set_server`` ``utf16`` or
  ``utf32`` or ``ucs2`` if you choose rsync as a state transfer method.
  The server will crash.

------------------------------------
 Dealing with Large Transactions
------------------------------------
.. _`Dealing with Large Transactions`:

TBD


------------------ 
 Isolation Levels
------------------
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

Galera uses transaction isolation on two levels:

- Locally, that is, on each node, transaction isolation works as
  with native InnoDB. You can use all levels. The default isolation
  level for InnoDB is ``REPEATABLE-READ``. 
- At the cluster level, between transactions processing at separate
  nodes, Galera implements a transaction level called ``SNAPSHOT ISOLATION``.
  The ``SNAPSHOT ISOLATION`` level is between the ``REPEATABLE READ``
  and ``SERIALIZABLE`` levels.

  The ``SERIALIZABLE`` transaction isolation level cannot be
  guaranteed in a multi-master use case, as :term:`Galera Replication`
  does not carry a transaction read set. Also, the ``SERIALIZABLE``
  transaction isolation level is vulnerable for multi-master
  conflicts. It holds read locks and any replicated write to a
  read locked row will cause the transaction to abort. Hence,
  it is recommended not to use it in a Galera cluster.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
