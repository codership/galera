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

In the case of a network failure, failure of more than half
of the cluster nodes, or a split-brain situation, your node(s)
may no longer deem that they are part of the :term:`Primary Component`
of the cluster. In other words, they suspect that there is
another primary component in the cluster which they have no
connection to.

If this is the case, all nodes return an *Unknown command* to
every query.

If you know that no other nodes of your cluster play the primary
component role, you can reset the quorum by rebootstrapping the
primary component as follows:

1. Choose the most up-to-date node. You can check this by reading
   the output command:
   
   ``SHOW STATUS LIKE 'wsrep_last_committed'``
   
   Choose the node with the highest value.
2. Once you have chosen the most up-to-date node, run the
   command below on it:
   
   ``SET GLOBAL wsrep_provider_options='pc.bootstrap=yes'``
   
3. The quorum is reset and:

   - The component with this node as a member will become
     the primary component
   - All nodes the component will synchronize to the most
     up-to-date node, and
   - The cluster starts to accept SQL requests again.
