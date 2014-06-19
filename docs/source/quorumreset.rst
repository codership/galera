======================
 Resetting the Quorum
======================
.. _`Resetting the Quorum`:

.. index::
   pair: Parameters; wsrep_last_committed
.. index::
   pair: Parameters; wsrep_provider_options
.. index::
   pair: Parameters; pc.bootstrap
.. index::
   single: Split-brain; Recovery
.. index::
   single: Primary Component; Nominating

There are occasions when your node or nodes may no longer consider themselves part of the :term:`Primary Component`.  For instance, in the event of network failure, the failure of more than half the cluster nodes, or a split-brain situation.  In other words, cases where the nodes suspect that there is another Primary Component in the cluster to which they are no longer connected.

When this is the case, all nodes return an ``Unknown command`` to each query.

-----------------------
How to Reset the Quorum
-----------------------

If you find yourself in this situation and you know that no other nodes in the cluster play the Primary Component role, you can reset the quorum by rebootstrapping the Primary Component.

To rebootstrap the Primary Component, complete the following steps:

1. Choose the most up to date node.  Run the following query on the database::

	SHOW STATUS LIKE 'wsrep_last_committed'

   And choose the database that returns the highest value to serve as the new Primary Component.

2. To bootstrap this node, run the following query::

	SET GLOBAL wsrep_provider_options='pc.bootstrap=yes'

This resets the quorum.  The component that has this node as a member becomes the new Primary Component.  All nodes in this component synchronize to the most up to date node.  The cluster begins to again accept SQL requests.
