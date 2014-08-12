======================================
 Dealing with Multi-Master Conflicts
======================================
.. _`Dealing with Multi-Master Conflicts`:

This chapter describes how Galera Cluster deals with conflicts in multi-master database environments.  In practice, the conflict scenario that must be addressed is row conflicts on different nodes.  In a multi-master replication system, where updates can be submitted to any database node, different nodes may try to update the same database row with different data.
  
Galera Cluster can cope with a situation such as this by using Certification Based Replication.

.. seealso:: Chapter :ref:`Certification Based Replication <Certification Based Replication>`.

-----------------------------------
 Diagnosing Multi-Master Conflicts
-----------------------------------

.. index::
   pair: Parameters; wsrep_debug
.. index::
   pair: Parameters; wsrep_local_bf_aborts
.. index::
   pair: Parameters; wsrep_local_cert_failures
.. index::
   pair: Parameters; cert.log_conflicts
.. index::
   pair: Logs; Debug log

You can log cluster wide conflicts by using the ``wsrep_debug=1`` variable value, which will log these conflicts and plenty of other information. You may see any of the following messages:

.. code-block:: text

     110906 17:45:01 [Note] WSREP: BF kill (1, seqno: 16962377), victim:  (140588996478720 4) trx: 35525064
     110906 17:45:01 [Note] WSREP: Aborting query: commit
     110906 17:45:01 [Note] WSREP: kill trx QUERY_COMMITTING for 35525064
     110906 17:45:01 [Note] WSREP: commit failed for reason: 3, seqno: -1

You can also monitor the parameters below::

	wsrep_local_bf_aborts
	wsrep_local_cert_failures

Setting the ``wsrep_provider_options="cert.log_conficts=1"`` will log messages like::

     7:51:13 [Note] WSREP: trx conflict for key (1,FLAT8)056eac38 0989cb96: source: cdeae866-d4a8-11e3-bd84-479ea1a1e941 version: 3 local: 1 state: MUST_ABORT flags: 1 conn_id: 160285 trx_id: 29755710 seqnos (l: 643424, g: 8749173, s: 8749171, d: 8749171, ts: 12637975935482109) <--X--> source: 5af493da-d4ab-11e3-bfe0-16ba14bdca37 version: 3 local: 0 state: APPLYING flags: 1 conn_id: 157852 trx_id: 26224969 seqnos (l: 643423, g: 8749172, s: 8749171, d: 8749170, ts: 12637839897662340)

------------------------------
 Autocommitting Transactions
------------------------------

.. index::
   pair: Parameters; wsrep_retry_autocommit

When a conflicting transaction is rolled back, the client application sees a deadlock error. The client application *should* try to re-commit the deadlocked transactions, but not all client applications have this logic inbuilt.

Nevertheless, Galera Cluster can re-try to autocommit deadlocked transactions on behalf of the client application. The ``wsrep_retry_autocommit`` parameter defines how many times the transaction is retried before returning a deadlock error.

.. note:: Retrying only applies to autocommit transactions, as retrying is not safe for multi-statement transactions.

---------------------------------------
 Working Around Multi-Master Conflicts
---------------------------------------

.. index::
   pair: Parameters; wsrep_retry_autocommit

Galera Cluster automatically resolves multi-master conflicts. However, you can try to minimize the amount of multi-master conflicts:

- Analyze the hot-spot. See if you can change the application logic to catch deadlock exceptions and use retrying logic.

- Use ``wsrep_retry_autocommit`` and see if it helps.

- Limit the number of master nodes or switch to a master-slave model.
  
  .. note:: If you can filter out the access to the hot spot table, it is enough to treat writes only to the hot spot table as master-slave.
