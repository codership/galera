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

The table below lists some symptoms and solutions for
troubleshooting purposes.

+--------------------------------------------------------+-------------------------------------------------------------+
| Symptom                                                | Solution                                                    |
+========================================================+=============================================================+
| If you use rsync for state transfer and a node crashes |  Find the orphan rsync process and kill it manually.        |
| before the state transfer is over, the rsync process   |                                                             |
| may hang forever, occupying the port and not allowing  |                                                             |
| to restart the node. The problem will show up as       |                                                             |
| *port in use* in the server error log.                 |                                                             |
+--------------------------------------------------------+-------------------------------------------------------------+
| If you use *mysqldump* for state transfer, and it      | Read the pseudo-statement within the ``SQL SYNTAX``         |
| fails, an ``SQL SYNTAX`` error is written in the       | resynchronizes with the primary component.                  |
| server error log. This error is only an indication of  |                                                             |
| the error. The pseudo-statement within the             |                                                             |
| ``SQL SYNTAX`` error contains the actual error         |                                                             |
| message.                                               |                                                             |
+--------------------------------------------------------+-------------------------------------------------------------+
| After a temporary split, if the Primary Component was  | This situation will be cleared after the node automatically |
| still reachable and its state was modified,            | while.                                                      |
| resynchronization occurs. In resynchronization, nodes  |                                                             |
| on the other part of the cluster drop all client       |                                                             |
| connections. The connections get the *Unknown command* |                                                             |
| error.                                                 |                                                             |
+--------------------------------------------------------+-------------------------------------------------------------+
| Every query returns "Unknown command".                 | You can bypass the ``wsrep_provider`` check by switching    |
|                                                        | the wsrep service off by using the command:                 |
| This phenomenon takes place if you have explicitly     |                                                             |
| specified the ``wsrep_provider`` variable, but the     | ``mysql> SET wsrep_on=0;``                                  |
| wsrep provider rejects service, for example, because   |                                                             |
| the node is not connected to the cluster Primary       | This command instructs *mysqld* to ignore the               |
| Component (the ``wsrep_cluster_address`` parameter     | ``wsrep_provider setting`` and to behave as a               |
| may be unset, or there can be networking issues).      | standalone MySQL server. This may lead to data              |
| In this case, the node is considered to be unsynced    | inconsistency with the rest of the cluster, which, on the   |
| with the global state and unable to serve SQL requests | other hand, may be a desirable result for, for example,     |
| except ``SET`` and/or ``SHOW``.                        | modifying "local" tables.                                   |
|                                                        |                                                             |
|                                                        | If you know that no other nodes of your cluster form        |
|                                                        | Primary Component, rebootstrap the Primary Component as     |
|                                                        | follows:                                                    |
|                                                        |                                                             |
|                                                        | 1. Choose the most up-to-date node by checking the output   |
|                                                        |    of ``SHOW STATUS LIKE 'wsrep_last_committed'``. Choose   |
|                                                        |    the node with the highest value.                         |
|                                                        | 2. Run                                                      |
|                                                        |    ``SET GLOBAL wsrep_provider_options='pc.bootstrap=yes'`` |
|                                                        |    on it.                                                   |
|                                                        |                                                             |
|                                                        | The component this node is part of will become a Primary    | 
|                                                        | Component, and all nodes in it will synchronize to the most |
|                                                        | up-to-date one and start accepting SQL requests again.      |
+--------------------------------------------------------+-------------------------------------------------------------+
| Users (name, host, password) changes are not           | You have tried to update the *mysql.user* table directly.   |
| replicated to the cluster.                             | Use the ``GRANT`` command.                                  |
|                                                        |                                                             |
|                                                        | Currently, replication only works with the InnoDB storage   |
|                                                        | engine. Any writes to tables of other types, including      |
|                                                        | system (mysql.*) tables, are not replicated. However, DDL   |
|                                                        | statements are replicated on statement level, and changes   |
|                                                        | to mysql.* tables will get replicated that way. You can     |
|                                                        | safely issue commands such as ``CREATE USER...`` or         |
|                                                        | or ``GRANT...``, but issuing commands such as ``INSERT INTO |
|                                                        | mysql.user...`` will not be replicated. As a rule,          |
|                                                        | non-transactional engines cannot be supported in            |
|                                                        | multi-master replication.                                   |
+--------------------------------------------------------+-------------------------------------------------------------+
| Cluster stalls when running the ``ALTER`` command on   | This is a side effect of a multi-master and several         |
| an unused table.                                       | appliers scenario. The system needs to control when the DDL |
|                                                        | ends in relation to other transactions in order to          |
|                                                        | deterministically detect conflicts and schedule parallel    |
|                                                        | appliers. Effectively, the DDL commands must be  executed   |
|                                                        | in isolation. *Galera Cluster for MySQL* has a 65K window   |
|                                                        | tolerance where transactions can be applied in parallel,    |
|                                                        | but if an ALTER command takes too long, the cluster has to  |
|                                                        | wait.                                                       |
|                                                        |                                                             |
|                                                        | You cannot help this situation. However, if you can         |
|                                                        | guarantee that no other session will try to modify the      |
|                                                        | table AND that there are no other DDLs running, you can:    |
|                                                        |                                                             |
|                                                        | 1. Set ``wsrep_OSU_method=RSU``                             |
|                                                        | 2. Run the ``ALTER`` command                                |
|                                                        | 3. Set ``wsrep_OSU_method=TOI``                             |
|                                                        |                                                             |
|                                                        | Do this on each node in turn.                               |
+--------------------------------------------------------+-------------------------------------------------------------+
