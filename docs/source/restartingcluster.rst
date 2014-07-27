================================
Restarting the Cluster
================================
.. _`Restarting the Cluster`:

Occasionally, you may have to restart the entire Galera Cluster.  This may happen, for example, in the case of a power failure where every node is shut down and you have no ``mysqld`` process at all.

To restart an entire Galera Cluster, complete the following steps:

1. Identify the node with the most advanced node state ID.

2. Start the most advanced node as the first node of the cluster.

3. Start the rest of the node as usual.


---------------------------------
Identifying the Most Advanced Node
---------------------------------
.. _`Identify Most Advanced Node`:

Identifying the most advacned node state ID is managed by comparing the :term:`Global Transaction ID` values on different nodes in your cluster.  You can find this in the ``grastate.dat`` file, located in the datadir for your database.

If the ``grastate.dat`` file looks like the example below, you have found the most advanced node state ID:

.. code-block:: text

	# GALERA saved state
	version: 2.1
	uuid:    5ee99582-bb8d-11e2-b8e3-23de375c1d30
	seqno:   8204503945773
	cert_index:

To find the sequence number of the last committed transaction, run ``mysqld`` with the ``--wsrep-recover`` option.  This recovers the InnoDB table space to a consistent state, prints the corresponding Global Transaction ID value into the error log, and then exits.  For example:

.. code-block:: console

	130514 18:39:13 [Note] WSREP: Recovered position: 5ee99582-bb8d-11e2-b8e3-23de375c1d30:8204503945771

This value is the node state ID.  You can use it to manually update the ``grastate.dat`` file, by entering it for the ``seqno`` field, or let ``mysql_safe`` recover automatically and pass the value to your database server the next time you start it.

--------------------------------------
Identifying Crashed Nodes
--------------------------------------
.. _`Identify Crashed Node`:

If the ``grastate.dat`` file looks like the example below, the node has either crashed during execution of a non-transactional operation, (such as ``ALTER TABLE``), or borted due to a database inconsistency:

.. code-block:: text

	# GALERA saved state
	version: 2.1
	uuid:    5ee99582-bb8d-11e2-b8e3-23de375c1d30
	seqno:   -1
	cert_index:

It is possible for you to recover the Global Transaction ID of the last committed transaction from InnoDB, as described above, but the recovery is rather meaningless.  After the crash, the node state is probably corrupted and may not even prove functional.  

In the event that there are no other nodes in the cluster with a well-defined state, then there is no need to preserve the node state ID.  You must perform a thorough database recovery procedure, similar to that used on standalone database servers.  Once you recover one node, use it as the first node in a new cluster.

If there are no other nodes with a well-defined state, you must perform a thorough database recovery procedure, (similar to that used on standalone database servers), on one node, then use it as the first node in a new cluster.

You can still recover the node state ID of the last committed transaction from InnoDB as described above; however, the recover is rather meaningless.  The node state is probably corrupted and may not even be functional.

