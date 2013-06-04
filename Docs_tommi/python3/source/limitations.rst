====================================================
 Differences to a Standalone MySQL Server
====================================================
.. _`Differences to Standalone MySQL Server`:

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