=========================
 Backing Up Cluster Data
=========================
.. _`backing-up-cluster-data`:

.. index::
   pair: Logs; mysqld error log
.. index::
   pair: Parameters; gmcast.listen_addr
.. index::
   pair: Parameters; wsrep_cluster_name
.. index::
   pair: Parameters; wsrep_node_name
.. index::
   single: Galera Arbitrator

You can perform backups with Galera Cluster at the same regularity as with the standard MySQL server, using a backup scrip.  Given that replication ensures that all nodes carry the same data, running the script on one node backs up the data on all nodes in the cluster.

The problem with such backups is that they lack a :term:`Global Transaction ID`.  You can use backups of this kind to recover data, but they are insufficient for use in recovering nodes to a well-defined state.  Furthermore, some backup procedures can block cluster operations for the duration of the backup.

Getting backups with the associated :term:`Global Transaction ID` requires a different approach.

----------------------
Replication as Backup
----------------------

.. _`replication-backup`:

In order to associate a :term:`Global Transaction ID` with your data backups, you need to use a special backup script in the ``mysqld`` path of the donor node and the Galera Arbitrator, to trigger a state snapshot transfer through the cluster.  

For example,

.. code-block:: console

   $ garbd --address gcomm://192.168.1.2?gmcast.listen_addr=tcp://0.0.0.0:444 \
     --group example_cluster --donor example_donor --sst backup

When this command runs, the Galera Arbitrator triggers the **wsrep_sst_backup** script in the ``mysqld`` directory on the donor node.  The script initiates a state snapshop transfer, sending the results to the Galera Arbitrator on the receiving machine.

In the command, ``?gmcast.listen_addr=tcp://0.0.0.0:4444`` is an arbitrary listen socket address that the Galera Arbitrator opens to communicate with the cluster.  You only need to specify this in the event that the default socket address, (that is, ``0.0.0.0:4567``), is busy.

.. note:: When you run the **garbd** script, it may exit immediately with confusing diagnostics, even after it manages a successful state snapshot transfer request.  This is not a failure.  The donor **mysqld** still runs the backup script.  You can monitor it's progress through the error and script logs on the donor machine.


