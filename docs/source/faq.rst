============================
 Frequently Asked Questions
============================
.. _`Frequently Asked Questions`:

This chapter lists a number of frequently asked questions
on *Galera Cluster for MySQL* and other related matters.

.. rubric:: What does "commit failed for reason: 3" mean?

.. _`What does "commit failed for reason: 3" mean?`:

Occasionally, a slave thread tries to apply a replicated writeset
and finds a lock conflict with a local transaction, which may already
be in the commit phase. The local transaction is aborted and the
slave thread can proceed. This is a consequence of an optimistic
(expecting no row conflicts) transaction execution, and is expected
in a multi-master configuration. You may see any of the following
messages:

  ::
  
      110906 17:45:01 [Note] WSREP: BF kill (1, seqno: 16962377), victim:  (140588996478720 4) trx: 35525064
      110906 17:45:01 [Note] WSREP: Aborting query: commit
      110906 17:45:01 [Note] WSREP: kill trx QUERY_COMMITTING for 35525064
      110906 17:45:01 [Note] WSREP: commit failed for reason: 3, seqno: -1


.. note:: The log example is taken from a debug log (``wsrep_debug=1``).

To avoid such conflicts, you can:

- Use the cluster in a master-slave configuration, that is, direct all writes to a single node
- Use the same approaches as for master-slave read/write splitting
