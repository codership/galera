====================================================
 Differences from a Standalone MySQL Server
====================================================
.. _`galera-mysql-differences`:

.. index::
   pair: Errors; ER_LOCK_DEADLOCK

Although Galera Cluster is built on providing write-set replication to MySQL and related database systems, there are certain key differences between how it handles and the standard standalone MySQL server.

------------------------
Server Differences
------------------------
.. _`server-difference`:

Using a server with Galera Cluster is not the same as one with MySQL.  Galera Cluster does not support the same range of operating systems as MySQL, and there are differences in how it handles binary logs and character sets.


^^^^^^^^^^^^^^^^^^^^^^^^^
Operating System Support
^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`os-support`:

Galera Cluster requires that you use Linux or a similar UNIX-like operating system.  Binary packages are not supplied for FreeBSD, Solaris and Mac OS X.  There is no support available for Microsoft Windows.




^^^^^^^^^^^^^^^^^^^
Binary Log Support
^^^^^^^^^^^^^^^^^^^
.. _`binlog-support`:

Do not use the ``binlog-do-db`` and ``binlog-ignore-db`` options.

These binary log options are only supported for :abbr:`DML (Data Manipulation Language)` statements.  They provide no support for :abbr:`DDL (Data Definition Language)` statements.  This creates a discrepancy in the binary logs and will cause replication to abort.

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Unsupported Character Sets
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`unicode-support`:

Do not use the ``character_set_server`` with ``utf16``, ``utf32`` or ``ucs2``.

When you use **rsync** for state snapshot transfers, use of these unsupported character sets can cause the server to crash.  

.. note:: This is also a problem when you use automatic donor selection in your cluster, as the cluster may choose to use **rsync** on its own.


-------------------------------------
Differences in Table Configurations
-------------------------------------
.. _`db-config-limitations`:

There are certain features and configurations available in MySQL that do not work as expected in Galera Cluster, such as storage engine support, certain queries and the query cache.

^^^^^^^^^^^^^^^^^^^^^^^^^^^
Storage Engine Support
^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`storage-engine-support`:

Galera Cluster requires the InnoDB storage engine.  Writes made to tables of other types, including the system ``mysql-*`` tables, do not replicate to the cluster.

That said, :abbr:`DDL (Data Definition Language)` statements do replicate at the statement level, meaning that changes made to the ``mysql-*`` tables do replicate that way.

What this means is that if you were to issue a statement like

.. code-block:: mysql

   CREATE USER 'stranger'@'localhost'
     IDENTIFIED BY 'password';

or, like

.. code-block:: mysql

   GRANT ALL ON strangedb.* TO 'stranger'@'localhost';

the changes made to the ``mysql-*`` tables would replicate to the cluster.  However, if you were to issue a statement like

.. code-block:: mysql

   INSERT INTO mysql.user (Host, User, Password)
      VALUES ('localhost', 'stranger', 'password');

the changes would not replicate.

.. note:: In general, non-transactional storage engines cannot be supported in multi-master replication.

^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Tables without Primary Keys
^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`table-without-pk`:

Do not use tables without a primary key.

When tables lack a primary key, rows can appear in different order on different nodes in your cluster.  As such, queries like ``SELECT...LIMIT...`` can return different results.  Additionally, on such tables the ``DELETE`` statement is unsupported.

.. note:: If you have a table without a primary key, it is always possible to add an ``AUTO_INCREMENT`` column to the table without breaking your application.

^^^^^^^^^^^^^^^^^^^^^^^^^^
Table Locking
^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`unsupported-queries`:

Galera Cluster does not support table locking, as they conflict with multi-master replication.  As such, the ``LOCK TABLES`` and ``UNLOCK TABLES`` queries are not supported.  This also applies to lock functions, such as ``GET_LOCK()`` and ``RELEASE_LOCK()...`` for the same reason.


^^^^^^^^^^^^^^^^^^^^^^^^
Query Logs
^^^^^^^^^^^^^^^^^^^^^^^^
.. _`query-log-support`:

You cannot direct query logs to a table.  If you would like to enable query logging in Galera Cluster, you must forward the logs to a file.

.. code-block:: ini

   log_output = FILE

Use ``general_log`` and ``general_log_file to choose query logging and to set the filename for your log file.

^^^^^^^^^^^^^^^^^^^^
Query Cache
^^^^^^^^^^^^^^^^^^^^
.. _`query-cache`:

Do not use query cache.


----------------------------
Differences in Transactions
----------------------------
.. _`diff-transactions`:

There are some differences in how Galera Cluster handles transactions from MySQL, such as :abbr:`XA (eXtended Architecture)` transactions and limitations on transaction size.

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Distributed Transaction Processing
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`xa-transactions`:

While the standard MySQL server does provide support for distributed transaction processing, using the Open Group :abbr:`XA (eXtended Architecture)` standard, it is not available for Galera Cluster, given that it can lead to possible rollbacks on commit.

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Transaction Size
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`transaction-size`:

Although Galera Cluster does not explicitly limit the transaction size, the hardware you run it on does impose a size limitation on your transactions.  Nodes process write-sets in a single memory-resident buffer.  As such, extremely large transactions, such as ``LOAD DATA`` can adversely effect node performance.

You can avoid situations of this kind using the ``wsrep_max_ws_rows`` and the ``wsrep_max_ws_size`` parameters.  Limit the transaction rows to 128 KB and the transaction size to 1 GB. 

If necessary, you can increase these limits.

.. seealso:: For more information on these parameters, see their Reference entries, at :ref:`wsrep_max_ws_rows <wsrep_max_ws_rows>` and :ref:`wsrep_max_ws_size <wsrep_max_ws_size>`.



^^^^^^^^^^^^^^^^^^^^^^^^
Transaction Commits
^^^^^^^^^^^^^^^^^^^^^^^^

Galera Cluster uses at the cluster-level optimistic concurrency control, which can result in transactions that issue a ``COMMIT`` aborting at that stage.

For example, say that you have two transactions that will write to the same rows, but commit on separate nodes in the cluster and that only one of them can successfully commit.  The commit that fails is aborted, while the successful one replicates.

When aborts occur at the cluster level, Galera Cluster gives a deadlock error.

.. code-block:: mysql

   code (Error: 1213 SQLSTATE: 40001 (ER_LOCK_DEADLOCK)

If you receive this error, restart the failing transaction.  It will then issue on its own, without another to put it into conflict.

.. |---|   unicode:: U+2014 .. EM DASH
   :trim:
