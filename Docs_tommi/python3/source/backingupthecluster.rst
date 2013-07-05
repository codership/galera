=========================
 Backing up Cluster Data
=========================
.. _`Backing up Cluster Data`:

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

Galera Cluster backups can be performed just as
regular MySQL backups, using a backup script. Since all the
cluster nodes are identical, backing up one node backs up
the entire cluster.

However, such backups will have no global transaction IDs
associated with them. You can use these backups to recover
data, but they cannot be used to recover a Galera Cluster node to a
well-defined state. Furthermore, the backup procedure may
block the clusteroperation for the duration of backup, in
the case of a blocking backup.

You can associate a :term:`Global Transaction ID` with the backup
and avoid cluster stalling by carrying out the backup in the
same manner as a state snapshot transfer between the nodes.
For that:

- A special backup script must be installed in the *mysqld*
  path on the node that will be a backup source.
- The backup must be initiated through the
  Galera Cluster replication system.

For example, the command below will cause the chosen donor
node to the ``run wsrep_sst_backup`` script and pass the
corresponding :term:`Global Transaction ID` to it::

    /usr/bin/garbd --address gcomm://<donor node address>?gmcast.listen_addr=tcp://0.0.0.0:4444 --group <wsrep_cluster_name> --donor <wsrep_node_name on donor> --sst backup

.. note:: In the command, the ``?gmcast.listen_addr=tcp://0.0.0.0:4444``
          section is an arbitrary listen socket address that the ``garbd``
          will have to open to communicate with the cluster. You only
          have to specify it if the default socket address (``0.0.0.0:4567``)
          is busy.

.. note:: The ``garbd`` may immediately exit with confusing diagnostic
          after making a successful SST request. This is not a failure.
          The backup script is being run by the donor *mysqld*. You can
          monitor its progress in the donor *mysqld* error log and/or in
          the script log
