=============
 Limitations
=============
.. _`Limitations`:

Galera has the following limitations:

- Replication only works with the InnoDB storage engine. Any writes to tables
  of other types, including system (mysql.*) tables are not replicated. However,
  DDL statements are replicated at the statement level, and changes to mysql.*
  tables will get replicated that way. In other words, you can safely issue
  command ``CREATE USER...`` or ``GRANT...``, but issuing ``INSERT INTO mysql.user...``
  will not be replicated. In general, non-transactional engines cannot be supported
  in multi-master replication.
- Rows in tables without primary key may appear in different order on different
  nodes. As a result, command ``SELECT…LIMIT...`` may return slightly different
  sets. Do not use tables without a primary key. It is always possible to add an
  ``AUTO_INCREMENT`` column to a table without breaking your application.
- Unsupported queries:

    - ``LOCK``/``UNLOCK TABLES`` cannot be supported in master-master replication.
    - Lock functions (``GET_LOCK()``, ``RELEASE_LOCK()...``)

- Query logs cannot be directed to a table. If you enable query logging, you must
  forward the log to a file::
  
    log_output = FILE

  Use ``general_log`` and ``general_log_file`` to choose query logging and log file name.
- XA transactions can not be supported due to possible rollback on commit.
- Transaction size. While Galera does not explicitly limit the transaction size,
  a writeset is processed as a single memory-resident buffer and as a result,
  extremely large transactions (for example, ``LOAD DATA``) may adversely affect
  node performance. To avoid that, the ``wsrep_max_ws_rows`` and ``wsrep_max_ws_size``
  variables limit the transaction rows to 128K and the transaction size to 1Gb,
  by default. If necessary, you can increase those limits.