=========================
 Symptoms and Solutions
=========================
.. _`Symptoms and Solutions`:
.. index::
   pair: Troubleshooting; port in use
.. index::
   pair: Troubleshooting; unknown command
.. index::
   pair: Parameters; wsrep_provider

The table below lists some symptoms and solutions for
troubleshooting purposes.

+--------------------------------------------------------+-----------------------------------------------------------+
| Symptom                                                | Solution                                                  |
+========================================================+===========================================================+
| If you use rsync for state transfer and a node crashes |  Find the orphan rsync process and kill it manually.      |
| before the state transfer is over, the rsync process   |                                                           |
| may hang forever, occupying the port and not allowing  |                                                           |
| to restart the node. The problem will show up as       |                                                           |
| *port in use* in the server error log.                 |                                                           |
+--------------------------------------------------------+-----------------------------------------------------------+
| If you use mysqldump for state transfer, and it fails, | Read the pseudo-statement within the ``SQL SYNTAX``       |
| an ``SQL SYNTAX`` error is written in the server error | error.                                                    |
| log. This error is only an indication of the error.    |                                                           |
| The pseudo-statement within the ``SQL SYNTAX``         |                                                           |
| error contains the actual error message.               |                                                           |
+--------------------------------------------------------+-----------------------------------------------------------+
| After a temporary split, if the Primary Component was  | Wait. This situation is automatically cleared after a     |
| still reachable and its state was modified,            | while.                                                    |
| resynchronization occurs. In resynchronization, nodes  |                                                           |
| on the other part of the cluster drop all client       |                                                           |
| connections. The connections get the *unknown command* |                                                           |
| error.                                                 |                                                           |
+--------------------------------------------------------+-----------------------------------------------------------+
| ``SELECT ... FROM ...`` returns "Unknown command".     | You can bypass the ``wsrep_provider`` check by switching  |
|                                                        | the wsrep service off by using the command:               |
| This phenomenon takes place if you have explicitly     |                                                           |
| specified the ``wsrep_provider`` variable, but the     | ``mysql> SET wsrep_on=0;``                                |
| wsrep provider rejects service, for example, because   |                                                           |
| the node is not connected to the cluster primary       | This command instructs *mysqld* to ignore the             |
| component (the ``wsrep_cluster_address`` parameter     | ``wsrep_provider setting`` and to behave as a             |
| may be unset, or there can be networking issues).      | standalone MySQL server. This may lead to data            |
| In this case, the node is considered to be unsynced    | inconsistency with the rest of the cluster, which, on the |
| with the global state and unable to serve SQL requests | other hand, may be a desirable result for, for example,   |
| except ``SET`` and/or ``SHOW``.                        | modifying "local" tables.                                 |
+--------------------------------------------------------+-----------------------------------------------------------+

