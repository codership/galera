=========================
 Backing up Cluster Data
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

Galera Cluster can perform backups with the same regularity as MySQL backups, using a backup script.  Given that all cluster nodes are identical, backing up one node backs up the entire cluster.

However, such backups lack a :term:`Global Transaction ID`.  You can use these backups to recover data, but not to recover a Galera Cluster node to a well-defined state.  Furthermore, in the case of blocking backups, the backup procedure may block cluster operations for the duration of the backup.

You can associate a Global Transaction ID with the backup and avoid cluster stalling by carrying out the backup in the same manner as a state snapshot transfer between the nodes. 

To perform backups in this manner:

- On the node that will act as the backup source, install a special backup script in the **mysqld** path.

- Initiate backup through the Galera Cluster replication system.

For example, use the **garbd** to cause the donor node to run the ``wsrep_sst_backup`` script and pass the corresponding Global Transaction ID to it.

.. code-block:: console

    $ /usr/bin/garbd \
    	--address gcomm://donor_address?gmcast.listen_addr=tcp://0.0.0.0:4444 \
    	--group cluster_name --donor donor_cluster_name --sst backup

In the command, ``?gmcast.listen_addr=tcp=://0.0.0.0:4444`` is an arbitrary listen socket address that **grabd** opens to communicate with the cluster.  You only need to specify this in the event that the default socket address, (that is, ``0.0.0.0:4567``), is busy.

.. note:: The **garbd** script may exit immediately with confusing diagnostics after it makes a successful State Snapshot Transfer request.  This is not a failure.  The donor **mysqld** still runs the backup script.  You can monitor its progress in the donor **mysqld** error log and/or in the script log.

