=========================
 Symptoms and Solutions
=========================
.. _`Symptoms and Solutions`:
.. index::
   pair: Troubleshooting; Port in use
.. index::
   pair: Troubleshooting; Unknown command
.. index::
   pair: Parameters; wsrep_provider
.. index::
   pair: Parameters; wsrep_last_committed
.. index::
   pair: Errors; ER_UNKNOWN_COM_ERROR
.. index::
   pair: Logs; mysqld error log
.. index::
   pair: Primary Component; Nominating



--------------------------------------
Node Crashes during rsync SST
--------------------------------------
.. _`Node Crash rsync SST`:

If you use ``rsync`` for state transfers and a node crashes before the state transfer is over, the ``rsync`` process may hang forever, occupying the port and not allowing you to restart the node.  The problem will show up as ``port in use`` in the server error log.

**Solution**

Find the orphan ``rsync`` process and manually kill it.

---------------------------------------
SQL SYNTAX Errors during mysqldump SST
---------------------------------------
.. _`SQL Syntax Errors mysqldump SST`:

If you use ``mysqldump`` for state transfers and it fails, an ``SQL SYNTAX`` error is written in the server error log.  This error is only an indication of the actual error.  The pseudo-statement within the ``SQL SYNTAX`` error contains the actual error message.

**Solution**

Read the pseudo-statement within the ``SQL SYNTAX`` resynchronizes with the primary component.


--------------------------------------
Connection gives Unknown Command Errors
--------------------------------------
.. _`Connection gives Unknown Command Errors`:

After a temporary split, if the Primary Component was still reachable and its state was modified, resynchronization occurs.  In resynchronization, nodes on the other part of the cluster drop all client connections.  The connections get the ``Unknown command`` error.

**Solution**

This situation will be cleared after the node automatically while.


--------------------------------------
Query gives Unknown Command Errors
--------------------------------------
.. _`Query gives Unknown Command Errors`:

Every query returns ``Unknown command``.  

This phenomenon takes place if you have explicitly specified the ``wsrep_provider`` variable, but the wsrep Provider rejects service, for example, because the node is not connected to the cluster Primary Component (the ``wsrep_cluster_address`` parameter may be unset, or there can be networking issues),  The node is considered to be out of sync with the global state and unable to serve SQL requests except for ``SET`` and ``SHOW``.

**Solution**

You can bypass the ``wsrep_provider`` check by switching the wsrep service off with the following query:

.. code-block:: mysql

	SET wsrep_on=0;

This query instructs ``mysqld`` to ignore the ``wsrep_provider`` setting and to behave as a standalone database server.  This may lead to data inconsistency with the rest of the cluster, which, on the other hand, may be desirable result for modifying the "local" tables.

If you know that no other nodes of your cluster form the Primary Component, complete the following steps to rebootstrap the Primary Component:

1. Choose the most up-to-date node by checking the output of 

   .. code-block:: mysql

	SHOW STATUS LIKE 'wsrep_last_committed'  
   
   Choose the node with the highest value.

2. Run the following query to set the ``wsrep_provider_options``:

   .. code-block:: mysql

	SET GLOBAL wsrep_provider_options='pc.boostrap=YES';

The component this node is part of will become a Primary Component, which causes all nodes to synchronize with the most up-to-date node, allowing the cluster to accept SQL requests again.

--------------------------------------------
User Changes not Replicating to the Cluster
--------------------------------------------
.. _`User Changes not Replicating to the Cluster`:

Users (name, host, password) changes are not replicated to the cluster.

**Solution**

You have tried to update the ``mysql.user`` table directly.  Use the ``GRANT`` command.

Currently, replication only works with the InnoDB storage engine.  Any writes to tables of other types, including system (``mysql.*``) tables, are not replicated.  However, DDL statements are replicated on the statement level, and changes to ``mysql.*`` tables will get replicated that way.  You can safely issue commands as ``CREATE USER`` or ``GRANT``, but issuing commands such as ``INSERT INTO mysql.user`` will not be replicated.  

As a rule, non-transactional engines cannot be supported in multi-master replication.

--------------------------------------------
Cluster Stalls when ALTER runs on an Unused Table
--------------------------------------------
.. _`Cluster Stalls ALTER on Unused Table`:

Cluster stalls when running the ``ALTER`` query on an unused table.

**Solution**

This is a side effect of a multi-master and several appliers scenario.  The system needs to control when the DDL ends in relation to other transactions, in order to deterministically detect conflicts and schedule parallel appliers.  Effectively, the DDL command must be executed in isolation.  Galera Cluster has a 65K window of tolerance where transactions can be applied in parallel, but the cluster has to wait when ``ALTER`` commands take too long.

You cannot help this situation.  However, if you can guarantee that no other session will try to modify the table *and* that there are no other DDL's running, there is a workaround.

For each node in the cluster, complete the following steps:

1. Change the Online Schema upgrade method to rolling:

   .. code-block:: mysql

	SET 'wsrep_OSU_method=RSU';

2. Run the ``ALTER`` command.

3. Change the Online Schema upgrade method back to Total Order Isolation:

   .. code-block:: mysql

	SET 'wsrep_OSU_method=TOI';

The cluster now runs with the desired updates.

