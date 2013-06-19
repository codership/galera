=============
 Performance
=============
.. _`Performance`:

.. index::
   pair: Performance; Memory
.. index::
   pair: Performance; Swap size

In normal operation, a Galera node does not consume
much more memory than a regular MySQL server. Additional
memory is consumed for the certification index and uncommitted
write sets, but usually this is not noticeable in a typical
application. However, writeset caching during state transfer
makes an exception.

When a node is receiving a state transfer, it cannot process
and apply incoming write sets because it has no state to
apply them to yet. Depending on a state transfer mechanism
(for example, *mysqldump*), the node that sends the state
transfer may not be able to apply write sets. Instead, the
node must cache the write sets for a catch-up phase. Currently,
the write sets are cached in memory and, if the system runs out
of memory, either the state transfer will fail or the cluster
will block and wait for the state transfer to end.

To control memory usage for writeset caching, see chapter
:ref:`Optional Memory Settings <Optional Memory Settings>`.