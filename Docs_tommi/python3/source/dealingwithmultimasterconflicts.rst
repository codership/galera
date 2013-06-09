======================================
 Dealing with Multi-Master Conflicts
======================================
.. _`Dealing with Multi-Master Conflicts`:

This chapter describes how *Galera Cluster for MySQL*
deals with conflicts in multi-master database environments.
In practice, there a two conflict scenarios that must be
addressed:

- **Row conflicts on different nodes**

  In a multi-master replication system, where updates can
  be submitted to any database node, different nodes may
  try to update the same database row with different data.
- **Database hot spots**
  
  Database hot spots are rows where many transactions attempt to 
  write simultaneously. Typical hot spots are patterns such as
  ``queue`` or *ID allocation*.
  
  
*Galera Cluster for MySQL* can cope with situations such as
these by using Certification Based Replication. In Certification
Based Replication, a transaction is executed conventionally until
the commit point, under the assumption that there will be no
conflict. When the client issues a ``COMMIT`` command (but
before the actual commit has happened), all changes made to
the database by the transaction and the primary keys of changed
rows are collected into a writeset.

The writeset is replicated to the rest of the nodes.
After that, the writeset undergoes a deterministic certification
test (using the collected primary keys) on each node
(including the writeset originator node) which determines
if the writeset can be applied or not.

If the certification test fails, the writeset is dropped and
the original transaction is rolled back. If the test succeeds,
the transaction is committed and the writeset is applied on
the rest of the nodes.

See also chapter :ref:`Certification Based Replication <Certification Based Replication>`.

-----------------------------------
 Diagnosing Multi-Master Conflicts
-----------------------------------

.. index::
   pair: Parameters; wsrep_debug
   
.. index::
   pair: Parameters; wsrep_local_bf_aborts

.. index::
   pair: Parameters; wsrep_local_cert_failures

You can log cluster wide conflicts by using the ``wsrep_debug``
variable, which will log these conlicts and plenty of other
information. You can also monitor the parameters below:

- ``wsrep_local_bf_aborts``
- ``wsrep_local_cert_failures``

------------------------------
 Autocommitting Transactions
------------------------------

.. index::
   pair: Parameters; wsrep_retry_autocommit

When a conflicting transaction is rolled back, the client application
sees a deadlock error. The client application *should* try to re-commit
the deadlocked transactions, but not all client applications have this
logic inbuilt.

Nevertheless, *Galera Cluster for MySQL* can re-try to autocommit
deadlocked transactions on behalf of the client application. The
``wsrep_retry_autocommit`` parameter defines how manyt times the
transaction is retried before returning a deadlock error.

.. note:: Retrying only applies to autocommit transactions, as retrying
          is not safe for multi-statement transactions.

-----------------------------------
 Resolving Multi-Master Conflicts
-----------------------------------

.. index::
   pair: Parameters; wsrep_retry_autocommit

To resolve multi-master conflicts:

- Analyze the hot-spot. See if you can change the application
  logic to catch deadlock exceptions and use retrying logic.
- Use ``wsrep_retry_autocommit`` and see if it helps.
- Limit the number of master nodes.
- If these tips provide not help, see if you can change completely
  to a master-slave model.
  
  .. note:: If you can filter out the access to the hot spot
            table, it is enough to treat writes only to the hot
            spot table as master-slave.