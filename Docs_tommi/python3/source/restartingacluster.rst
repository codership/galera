==============================
 Restarting an Entire Cluster
==============================
.. _`Restarting an Entire Cluster`:
.. index::
   pair: Parameters; wsrep-recover
.. index::
   pair: Global Transaction ID; Recovery
   
Occarsionally, you may have to restart the entire
*Galera Cluster*. This may happen, for
example in the case of a power failure where every
*Galera Cluster* node is shut down and you have no *mysqld*
process at all. If this happens, proceed as
follows:

1. Identify the node with the most advanced node state ID.
   See chapter :ref:`Identifying the Most Advanced Node
   <Identifying the Most Advanced Node>`.
2. Start the node as the first node of the cluster.
3. Start the rest of the nodes as usual.

------------------------------------
 Identifying the Most Advanced Node
------------------------------------
.. _`Identifying the Most Advanced Node`:
.. index::
   pair: Logs; mysqld error log

You can identify the node with the most advanced node state ID
by comparing the :term:`Global Transaction ID` values on
different nodes.
     
Open the *grastate.dat* file, which, by default, is
stored in the MySQL datadir.
       
If the *grastate.dat* file looks like the example below,
you have found the most advanced node state ID:
       
   ::
   
       # GALERA saved state
       version: 2.1
       uuid:    5ee99582-bb8d-11e2-b8e3-23de375c1d30
       seqno:   8204503945773
       cert_index:

However, if the *grastate.dat* file looks like the
example below, the node has crashed:
       
   ::
       
       # GALERA saved state
       version: 2.1
       uuid:    5ee99582-bb8d-11e2-b8e3-23de375c1d30
       seqno:   -1
       cert_index:

To find the sequence number of the last committed transaction,
run *mysqld* with the ``--wsrep-recover`` option. This option
will recover the InnoDB table space to a consistent state, print
the corresponding GTID into the error log and exit. In the error
log, you can see something like this:
       
   ::
       
       130514 18:39:13 [Note] WSREP: Recovered position: 5ee99582-bb8d-11e2-b8e3-23de375c1d30:8204503945771
           
This is the state ID. Edit the *grastate.dat* file and
update the ``seqno`` field manually or let *mysqld_safe*
automatically recover it and pass it to the *mysqld* next
time you start it.

If the *grastate.dat* file looks like the example below,
the node has either crashed during execution of a
non-transactional operation (such as ``ALTER TABLE``)
or aborted due to a database inconsistency.

   ::
       
       # GALERA saved state
       version: 2.1
       uuid:    00000000-0000-0000-0000-000000000000
       seqno:   -1
       cert_index:

You still can recover the ID of the last committed transaction
from InnoDB as described above. However, the recovery is rather
meaningless as the node state is probably corrupted and may not
even be functional. If there are no other nodes with a well defined
state, a thorough database recovery procedure (similar to that
on a standalone MySQL server) must be performed on one of the
nodes, and this node should be used as a seed node for new cluster.
If this is the case, there is no need to preserve the state ID.
